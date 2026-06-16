#include "web_auth.h"

#include <Arduino.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <lwip/sockets.h>

#include <cstdint>
#include <cstring>

#include "auth_config.h"
#include "auth_store.h"
#include "client_key.h"
#include "crypto_mbedtls.h"
#include "csrf.h"
#include "login_throttle.h"
#include "password.h"
#include "security_headers.h"
#include "session.h"
#include "setup_state.h"

namespace web {

namespace {

constexpr char     kUser[]       = "admin";   // single admin identity
constexpr size_t   kNonceBytes   = 9;         // 12 base64url chars
constexpr size_t   kSecretBytes  = 32;
constexpr size_t   kCsrfRandBytes = 16;
constexpr size_t   kMaxBody      = 4096;

config::Settings     g_settings;
bool                 g_secureCookies = true;   // == g_settings.https
std::string          g_sessionSecret;          // RAM-only, per-boot; never persisted
runtime::CryptoMbedTls g_crypto;
runtime::AuthStore*  g_store = nullptr;         // device singleton; lives for the run
runtime::LoginThrottle g_throttle;

uint64_t nowMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

std::string headerValue(httpd_req_t* req, const char* name) {
	size_t len = httpd_req_get_hdr_value_len(req, name);
	if (len == 0)
		return {};
	std::string v(len + 1, '\0');
	if (httpd_req_get_hdr_value_str(req, name, &v[0], v.size()) != ESP_OK)
		return {};
	v.resize(len);
	return v;
}

bool cookieValue(httpd_req_t* req, const char* name, std::string& out) {
	char   buf[320];  // session/CSRF tokens are well under this
	size_t len = sizeof(buf);
	if (httpd_req_get_cookie_val(req, name, buf, &len) != ESP_OK)
		return false;
	out.assign(buf);  // NUL-terminated by the API
	return true;
}

uint32_t peerIpv4(httpd_req_t* req) {
	int fd = httpd_req_to_sockfd(req);
	if (fd < 0)
		return 0;
	struct sockaddr_storage ss;
	socklen_t               len = sizeof(ss);
	if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&ss), &len) != 0)
		return 0;
	if (ss.ss_family == AF_INET)
		return ntohl(reinterpret_cast<struct sockaddr_in*>(&ss)->sin_addr.s_addr);
	if (ss.ss_family == AF_INET6) {
		const auto* a6 = reinterpret_cast<struct sockaddr_in6*>(&ss);
		if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr)) {
			const uint8_t* b = reinterpret_cast<const uint8_t*>(&a6->sin6_addr);
			return (uint32_t(b[12]) << 24) | (uint32_t(b[13]) << 16) |
			       (uint32_t(b[14]) << 8) | uint32_t(b[15]);
		}
	}
	return 0;
}

uint32_t clientKey(httpd_req_t* req) {
	return runtime::deriveClientKey(peerIpv4(req), g_settings.trustedProxy,
	                                headerValue(req, "X-Real-IP"),
	                                headerValue(req, "X-Forwarded-For"));
}

int hexVal(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

std::string urlDecode(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		char c = s[i];
		if (c == '+') {
			out.push_back(' ');
		} else if (c == '%' && i + 2 < s.size() && hexVal(s[i + 1]) >= 0 && hexVal(s[i + 2]) >= 0) {
			out.push_back(static_cast<char>((hexVal(s[i + 1]) << 4) | hexVal(s[i + 2])));
			i += 2;
		} else {
			out.push_back(c);
		}
	}
	return out;
}

// Pull one application/x-www-form-urlencoded field; false if absent.
bool formField(const std::string& body, const std::string& key, std::string& out) {
	size_t i = 0;
	while (i < body.size()) {
		size_t amp  = body.find('&', i);
		size_t end  = (amp == std::string::npos) ? body.size() : amp;
		size_t eq   = body.find('=', i);
		std::string k = (eq == std::string::npos || eq > end) ? body.substr(i, end - i)
		                                                      : body.substr(i, eq - i);
		if (urlDecode(k) == key) {
			std::string v = (eq == std::string::npos || eq > end) ? std::string()
			                                                     : body.substr(eq + 1, end - eq - 1);
			out = urlDecode(v);
			return true;
		}
		if (amp == std::string::npos)
			break;
		i = amp + 1;
	}
	return false;
}

bool readBody(httpd_req_t* req, std::string& out) {
	out.clear();
	size_t total = req->content_len;
	if (total > kMaxBody)
		return false;
	out.resize(total);
	size_t got = 0;
	while (got < total) {
		int r = httpd_req_recv(req, &out[got], total - got);
		if (r == HTTPD_SOCK_ERR_TIMEOUT)
			continue;
		if (r <= 0)
			return false;
		got += static_cast<size_t>(r);
	}
	return true;
}

esp_err_t sendJson(httpd_req_t* req, const char* status, const std::string& body) {
	httpd_resp_set_status(req, status);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t sendForbidden(httpd_req_t* req) {
	return sendJson(req, "403 Forbidden", "{\"ok\":false,\"error\":\"forbidden\"}");
}

// Binding bound to the live session if one is valid, else empty (the pre-auth login/setup case).
std::string sessionBinding(httpd_req_t* req) {
	std::string sid;
	if (!cookieValue(req, runtime::sessionCookieName(g_secureCookies), sid))
		return {};
	runtime::SessionResult s = runtime::verifySession(g_crypto, g_sessionSecret, sid, nowMs());
	return s.valid ? s.binding : std::string();
}

bool checkCsrf(httpd_req_t* req, const std::string& body, const std::string& binding) {
	std::string formTok;
	formField(body, "csrf", formTok);
	std::string cookieTok;
	cookieValue(req, runtime::csrfCookieName(g_secureCookies), cookieTok);
	return runtime::verifyCsrf(g_crypto, g_sessionSecret, binding, cookieTok, formTok);
}

esp_err_t setSessionCookie(httpd_req_t* req, std::string& holder, const std::string& token) {
	holder = runtime::sessionCookie(token, g_secureCookies);  // must outlive the send
	return httpd_resp_set_hdr(req, "Set-Cookie", holder.c_str());
}

// --- handlers ---

esp_err_t handleCsrf(httpd_req_t* req) {
	uint8_t rand[kCsrfRandBytes];
	esp_fill_random(rand, sizeof(rand));
	std::string tok = runtime::mintCsrf(g_crypto, g_sessionSecret, sessionBinding(req), rand,
	                                    sizeof(rand));
	std::string cookie = runtime::csrfCookie(tok, g_secureCookies);  // outlives the send
	httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	return httpd_resp_send(req, tok.c_str(), tok.size());
}

esp_err_t handleLogin(httpd_req_t* req) {
	std::string body;
	if (!readBody(req, body))
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid\"}");
	if (!checkCsrf(req, body, ""))  // no session yet at login
		return sendForbidden(req);

	uint32_t now = static_cast<uint32_t>(nowMs());
	uint32_t key = clientKey(req);
	if (g_throttle.isLocked(key, now)) {
		uint32_t retry = g_throttle.timeRemainingMs(key, now);
		return sendJson(req, "200 OK",
		                "{\"ok\":false,\"error\":\"locked\",\"retryMs\":" + std::to_string(retry) + "}");
	}

	std::string password, record;
	formField(body, "password", password);
	bool ok = g_store && g_store->getCredential(record) &&
	          runtime::verifyPassword(g_crypto, password, record);
	if (!ok) {
		g_throttle.recordFailure(key, now);
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"invalid\"}");
	}
	g_throttle.recordSuccess(key, now);

	std::string holder;
	setSessionCookie(req, holder,
	                 runtime::mintSession(g_crypto, g_sessionSecret, kUser, nowMs()));
	return sendJson(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handleSetup(httpd_req_t* req) {
	std::string body;
	if (!readBody(req, body))
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"nonce\"}");
	if (!checkCsrf(req, body, ""))  // setup runs pre-credential, no session
		return sendForbidden(req);

	std::string nonce, storedNonce;
	formField(body, "nonce", nonce);
	bool credPresent = g_store && g_store->hasCredential();
	if (g_store)
		g_store->getNonce(storedNonce);
	if (!runtime::setupAllowed(credPresent, nonce, storedNonce, 0, nowMs()))
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"nonce\"}");

	std::string password;
	formField(body, "password", password);
	if (password.size() < 8)  // mirrors the client rule; server is the authority
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"weak\"}");

	uint8_t salt[runtime::kSaltLen];
	esp_fill_random(salt, sizeof(salt));
	std::string record = runtime::deriveCredential(g_crypto, password, salt, sizeof(salt),
	                                                runtime::kPbkdf2DefaultIterations);
	if (record.empty() || !g_store->setCredential(record))
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"weak\"}");
	g_store->clearNonce();

	std::string holder;
	setSessionCookie(req, holder,
	                 runtime::mintSession(g_crypto, g_sessionSecret, kUser, nowMs()));
	return sendJson(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handleLogout(httpd_req_t* req) {
	std::string body;
	readBody(req, body);
	if (!checkCsrf(req, body, sessionBinding(req)))  // authenticated: bind to the session
		return sendForbidden(req);

	std::string cookie = runtime::clearSessionCookie(g_secureCookies);  // outlives the send
	httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
	return sendJson(req, "200 OK", "{\"ok\":true}");
}

void registerUri(httpd_handle_t server, const char* path, httpd_method_t method,
                 esp_err_t (*fn)(httpd_req_t*)) {
	httpd_uri_t uri = {
		.uri      = path,
		.method   = method,
		.handler  = fn,
		.user_ctx = nullptr,
	};
	httpd_register_uri_handler(server, &uri);
}

bool endsWithHtml(const std::string& p) {
	return p.size() >= 5 && p.compare(p.size() - 5, 5, ".html") == 0;
}

bool hasValidSession(httpd_req_t* req) {
	std::string sid;
	if (!cookieValue(req, runtime::sessionCookieName(g_secureCookies), sid))
		return false;
	return runtime::verifySession(g_crypto, g_sessionSecret, sid, nowMs()).valid;
}

}  // namespace

bool sessionValid(httpd_req_t* req) {
	return hasValidSession(req);
}

bool csrfValid(httpd_req_t* req, const std::string& token) {
	std::string cookieTok;
	cookieValue(req, runtime::csrfCookieName(g_secureCookies), cookieTok);
	return runtime::verifyCsrf(g_crypto, g_sessionSecret, sessionBinding(req), cookieTok, token);
}

void authBegin(const config::Settings& settings) {
	g_settings      = settings;
	g_secureCookies = settings.https;

	uint8_t secret[kSecretBytes];
	esp_fill_random(secret, sizeof(secret));
	g_sessionSecret.assign(reinterpret_cast<const char*>(secret), sizeof(secret));

	g_store = new runtime::AuthStore();
	if (!g_store->ok()) {
		Serial.println("auth: NVS store unavailable; login/setup will fail");
		return;
	}
	if (!g_store->hasCredential()) {
		std::string nonce;
		if (!g_store->getNonce(nonce)) {
			uint8_t rb[kNonceBytes];
			esp_fill_random(rb, sizeof(rb));
			nonce = runtime::makeNonce(rb, sizeof(rb));
			g_store->setNonce(nonce);
		}
		Serial.printf("\nauth: first-boot setup code: %s\n", nonce.c_str());
		Serial.println("auth: enter it on the setup page to set the admin password\n");
	}
}

void registerAuthApi(httpd_handle_t server) {
	registerUri(server, "/api/csrf", HTTP_GET, handleCsrf);
	registerUri(server, "/api/login", HTTP_POST, handleLogin);
	registerUri(server, "/api/setup", HTTP_POST, handleSetup);
	registerUri(server, "/api/logout", HTTP_POST, handleLogout);
}

GateResult gate(httpd_req_t* req, const std::string& path) {
	if (!endsWithHtml(path))
		return GateResult::Allow;  // assets always served (login/setup pages need them)
	bool credential = g_store && g_store->hasCredential();
	if (!credential)
		return path == "/setup.html" ? GateResult::Allow : GateResult::RedirectSetup;
	if (!hasValidSession(req))
		return path == "/login.html" ? GateResult::Allow : GateResult::RedirectLogin;
	// authed: never show the login/setup pages again
	if (path == "/login.html" || path == "/setup.html")
		return GateResult::RedirectDashboard;
	return GateResult::Allow;
}

}  // namespace web
