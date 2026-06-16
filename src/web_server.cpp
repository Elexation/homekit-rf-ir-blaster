#include "web_server.h"

#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_https_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <cstdint>
#include <string>

#include "cert_store.h"
#include "request_policy.h"
#include "security_headers.h"
#include "web_assets.h"
#include "web_auth.h"
#include "web_config_api.h"

namespace web {

namespace {
config::Settings g_settings;            // stashed by begin(), read-only after start()
CertMaterial     g_cert;               // loaded by start() when https
httpd_handle_t   g_secure = nullptr;   // HTTPS listener, or null
httpd_handle_t   g_plain  = nullptr;   // :80 redirect when https on, else serves on listenPort
bool             g_started = false;    // connection callback fires per reconnect; start once

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

bool peerIsLoopback(httpd_req_t* req) {
	int fd = httpd_req_to_sockfd(req);
	if (fd < 0)
		return false;
	struct sockaddr_storage ss;
	socklen_t               len = sizeof(ss);
	if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&ss), &len) != 0)
		return false;
	if (ss.ss_family == AF_INET) {
		uint32_t ip = ntohl(reinterpret_cast<struct sockaddr_in*>(&ss)->sin_addr.s_addr);
		return (ip >> 24) == 127;  // 127.0.0.0/8
	}
	if (ss.ss_family == AF_INET6) {
		const auto* a6 = reinterpret_cast<struct sockaddr_in6*>(&ss);
		if (IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr))
			return true;
		// IPv4-mapped loopback (::ffff:127.x.x.x)
		if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr)) {
			const uint8_t* b = reinterpret_cast<const uint8_t*>(&a6->sin6_addr);
			return b[12] == 127;
		}
	}
	return false;
}

std::string pathOf(const char* uri) {
	std::string p(uri);
	size_t q = p.find('?');
	if (q != std::string::npos)
		p.resize(q);
	if (p == "/")
		p = "/dashboard.html";
	return p;
}

const web_assets::Asset* findAsset(const std::string& path) {
	for (size_t i = 0; i < web_assets::kAssetCount; ++i)
		if (path == web_assets::kAssets[i].path)
			return &web_assets::kAssets[i];
	return nullptr;
}

esp_err_t sendAsset(httpd_req_t* req, const web_assets::Asset* a, bool https) {
	httpd_resp_set_type(req, a->contentType);
	if (a->gzipped)
		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	for (size_t i = 0; i < runtime::kStaticSecurityHeaderCount; ++i)
		httpd_resp_set_hdr(req, runtime::kStaticSecurityHeaders[i].name,
		                   runtime::kStaticSecurityHeaders[i].value);
	std::string hsts;
	if (https) {
		hsts = runtime::hstsHeader();  // outlives the send below
		httpd_resp_set_hdr(req, "Strict-Transport-Security", hsts.c_str());
	}
	// PROGMEM is memory-mapped flash on ESP32: direct pointer read, no pgm_read.
	return httpd_resp_send(req, reinterpret_cast<const char*>(a->data), a->length);
}

// Auth-state redirect: 302 (not 301) so the browser never caches a login/setup bounce.
esp_err_t redirectTo(httpd_req_t* req, const char* location) {
	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", location);
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	return httpd_resp_send(req, nullptr, 0);
}

esp_err_t handle(httpd_req_t* req, runtime::Scheme scheme) {
	runtime::Request rr;
	rr.transportScheme = scheme;
	rr.host            = headerValue(req, "Host");
	rr.target          = req->uri;  // path + query
	rr.forwardedProto  = headerValue(req, "X-Forwarded-Proto");
	rr.isLoopback      = peerIsLoopback(req);

	runtime::Decision d = runtime::evaluate(rr, g_settings);
	if (d.action == runtime::Action::Redirect) {
		httpd_resp_set_status(req, "301 Moved Permanently");
		httpd_resp_set_hdr(req, "Location", d.location.c_str());  // outlives the send
		return httpd_resp_send(req, nullptr, 0);
	}
	if (d.action == runtime::Action::Reject) {
		httpd_resp_set_status(req, "403 Forbidden");
		return httpd_resp_send(req, "Forbidden", HTTPD_RESP_USE_STRLEN);
	}
	std::string path = pathOf(req->uri);
	switch (gate(req, path)) {
		case GateResult::RedirectSetup:     return redirectTo(req, "/setup.html");
		case GateResult::RedirectLogin:     return redirectTo(req, "/login.html");
		case GateResult::RedirectDashboard: return redirectTo(req, "/dashboard.html");
		case GateResult::Allow:             break;
	}
	const web_assets::Asset* a = findAsset(path);
	if (!a) {
		httpd_resp_set_status(req, "404 Not Found");
		return httpd_resp_send(req, "Not found", HTTPD_RESP_USE_STRLEN);
	}
	return sendAsset(req, a, scheme == runtime::Scheme::Https);
}

esp_err_t handleSecure(httpd_req_t* req) { return handle(req, runtime::Scheme::Https); }
esp_err_t handlePlain(httpd_req_t* req) { return handle(req, runtime::Scheme::Http); }

void registerCatchAll(httpd_handle_t server, esp_err_t (*fn)(httpd_req_t*)) {
	httpd_uri_t uri = {
		.uri      = "/*",
		.method   = HTTP_GET,
		.handler  = fn,
		.user_ctx = nullptr,
	};
	httpd_register_uri_handler(server, &uri);
}

// withApi: the serving instance (http mode) registers the API ahead of the catch-all;
// the :80 redirect-only instance (https mode) passes false and just redirects.
void startPlain(uint16_t port, uint16_t ctrlPort, esp_err_t (*fn)(httpd_req_t*), bool withApi) {
	httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
	conf.server_port     = port;
	conf.ctrl_port       = ctrlPort;  // must differ from any other running httpd instance
	conf.uri_match_fn    = httpd_uri_match_wildcard;
	conf.max_uri_handlers = 16;
	if (httpd_start(&g_plain, &conf) == ESP_OK) {
		if (withApi) {
			registerAuthApi(g_plain);    // specific paths before the wildcard catch-all
			registerConfigApi(g_plain);
		}
		registerCatchAll(g_plain, fn);
	} else {
		Serial.printf("web: plain httpd_start on %u failed\n", port);
	}
}

void startListeners() {
	authBegin(g_settings);  // secret + credential store + first-boot nonce, before any handler
	if (g_settings.https) {
		g_cert = loadOrCreateCert();
		if (!g_cert.ok) {
			Serial.println("web: cert load/generate failed; HTTPS server not started");
			return;
		}
		httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
		conf.servercert       = reinterpret_cast<const uint8_t*>(g_cert.certPem.c_str());
		conf.servercert_len   = g_cert.certPem.size() + 1;  // esp_tls wants the PEM NUL counted
		conf.prvtkey_pem      = reinterpret_cast<const uint8_t*>(g_cert.keyPem.c_str());
		conf.prvtkey_len      = g_cert.keyPem.size() + 1;
		conf.port_secure      = g_settings.listenPort;
		conf.httpd.uri_match_fn    = httpd_uri_match_wildcard;
		conf.httpd.max_uri_handlers = 16;
		// Browsers open more parallel asset requests than max_open_sockets; lru_purge would
		// kill an in-flight transfer to take each new one (the 16-socket lwip cap, shared with
		// HAP, blocks raising the limit). Queue the overflow instead of dropping assets.
		conf.httpd.lru_purge_enable = false;

		if (httpd_ssl_start(&g_secure, &conf) == ESP_OK) {
			registerAuthApi(g_secure);    // specific paths before the wildcard catch-all
			registerConfigApi(g_secure);
			registerCatchAll(g_secure, handleSecure);
		} else {
			Serial.println("web: httpd_ssl_start failed");
		}

		// :80 redirect. The SSL server's ctrl_port is the default+1 (32769), so the plain
		// server uses the default 32768; identical ctrl_ports fail the second httpd_start.
		startPlain(80, 32768, handlePlain, /*withApi=*/false);
	} else {
		startPlain(g_settings.listenPort, 32768, handlePlain, /*withApi=*/true);
	}

	Serial.printf("web: started (https=%d port=%u) free heap=%u\n",
	              (int)g_settings.https, g_settings.listenPort, (unsigned)ESP.getFreeHeap());
}

void startTask(void*) {
	startListeners();
	vTaskDelete(nullptr);  // one-shot: frees the 16 KB stack once the listeners are up
}
}  // namespace

void begin(const config::Settings& settings) {
	g_settings = settings;
}

void start(int) {
	if (g_started)
		return;
	g_started = true;
	// Cert keygen + TLS startup overflow the ~8 KB loop-task stack; run them on a
	// dedicated 16 KB task that self-deletes, keeping the connection callback light.
	if (xTaskCreate(startTask, "web_start", 16384, nullptr, 1, nullptr) != pdPASS) {
		Serial.println("web: failed to spawn start task");
		g_started = false;  // let a later reconnect retry
	}
}

}  // namespace web
