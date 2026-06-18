#include "onboarding.h"

#include "HomeSpan.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "auth_store.h"
#include "security_headers.h"
#include "setup_state.h"
#include "ui.h"
#include "web_assets.h"

namespace onboarding {

namespace {

constexpr uint32_t kPortalTimeoutMs = 10 * 60 * 1000;
constexpr char     kApPrefix[]      = "RF-IR Blaster ";
constexpr uint8_t  kBridgeCategory  = 2;  // HAP accessory category for a Bridge

// Finish persist (SRP keygen, NVS) runs on the loop task; the httpd ~4 KB stack is too tight.
enum class FinishState { Idle, Pending, Done, Failed };
// Live Wi-Fi check runs on the loop task too (STA connect blocks); the page polls.
enum class VerifyState { Idle, Testing, Ok, Fail };

httpd_handle_t     g_httpd  = nullptr;
DNSServer          g_dns;
std::string        g_apIp;
volatile bool      g_reboot = false;
std::string        g_networksJson = "[]";  // boot-time Wi-Fi scan; immutable once run() builds it

SemaphoreHandle_t  g_mx     = nullptr;
FinishState        g_fstate = FinishState::Idle;
std::string        g_ssid, g_pwd, g_code;   // finish inputs
std::string        g_qr, g_nonce, g_ferr;   // finish outputs
VerifyState        g_vstate = VerifyState::Idle;
std::string        g_vssid, g_vpwd;          // verify inputs
std::string        g_verifyIp;               // home-Wi-Fi IP from verify, shown on the done screen

bool eightDigits(const std::string& c) {
	if (c.size() != 8)
		return false;
	for (char ch : c)
		if (ch < '0' || ch > '9')
			return false;
	return true;
}

// The 12 setup codes HAP forbids (too simple); also skipped when generating.
bool isTrivialCode(const std::string& c) {
	static const char* kBad[] = {"00000000", "11111111", "22222222", "33333333",
	                             "44444444", "55555555", "66666666", "77777777",
	                             "88888888", "99999999", "12345678", "87654321"};
	for (const char* b : kBad)
		if (c == b)
			return true;
	return false;
}

bool validCode(const std::string& c) { return eightDigits(c) && !isTrivialCode(c); }

std::string makeRandomCode() {
	std::string c(8, '0');
	do {
		for (char& ch : c)
			ch = static_cast<char>('0' + (esp_random() % 10));
	} while (isTrivialCode(c));
	return c;
}

// --- form / body parsing (urlencoded) ---

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

bool formField(const std::string& body, const std::string& key, std::string& out) {
	size_t i = 0;
	while (i < body.size()) {
		size_t amp = body.find('&', i);
		size_t end = (amp == std::string::npos) ? body.size() : amp;
		size_t eq  = body.find('=', i);
		std::string k = (eq == std::string::npos || eq > end) ? body.substr(i, end - i)
		                                                      : body.substr(i, eq - i);
		if (urlDecode(k) == key) {
			std::string v = (eq == std::string::npos || eq > end)
			                    ? std::string()
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
	if (total > 1024)  // ssid + password + code only
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

// --- asset serving (gzipped PROGMEM, same headers as the main server minus HSTS) ---

const web_assets::Asset* findAsset(const std::string& path) {
	for (size_t i = 0; i < web_assets::kAssetCount; ++i)
		if (path == web_assets::kAssets[i].path)
			return &web_assets::kAssets[i];
	return nullptr;
}

esp_err_t sendAsset(httpd_req_t* req, const web_assets::Asset* a) {
	httpd_resp_set_type(req, a->contentType);
	if (a->gzipped)
		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
	for (size_t i = 0; i < runtime::kStaticSecurityHeaderCount; ++i)
		httpd_resp_set_hdr(req, runtime::kStaticSecurityHeaders[i].name,
		                   runtime::kStaticSecurityHeaders[i].value);  // no HSTS: plain HTTP
	return httpd_resp_send(req, reinterpret_cast<const char*>(a->data), a->length);
}

// --- one-time Wi-Fi scan (datalist source) ---

void appendJsonString(std::string& out, const std::string& s) {
	out.push_back('"');
	for (unsigned char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (c < 0x20) {
					char buf[7];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out.push_back(static_cast<char>(c));
				}
		}
	}
	out.push_back('"');
}

// Scan before the SoftAP is up so the channel sweep can't drop a portal client;
// async so the watchdog stays fed.
void scanNetworks() {
	WiFi.scanNetworks(true);
	uint64_t t0 = esp_timer_get_time();
	int16_t n;
	while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING &&
	       (esp_timer_get_time() - t0) < 8000000ULL) {
		esp_task_wdt_reset();
		delay(50);
	}
	std::string json = "[";
	std::vector<std::string> seen;  // same SSID appears once per band; keep the first
	for (int16_t i = 0; n > 0 && i < n && seen.size() < 24; ++i) {
		String raw = WiFi.SSID(i);
		if (raw.isEmpty() || raw.length() > 32)  // hidden APs broadcast empty; cap to input max
			continue;
		std::string s(raw.c_str());
		bool dup = false;
		for (const std::string& p : seen)
			if (p == s) { dup = true; break; }
		if (dup)
			continue;
		if (!seen.empty())
			json.push_back(',');
		seen.push_back(s);
		appendJsonString(json, s);
	}
	json.push_back(']');
	WiFi.scanDelete();
	g_networksJson = json;
}

// --- handlers ---

esp_err_t handleState(httpd_req_t* req) {
	return sendJson(req, "200 OK", "{\"code\":\"" + makeRandomCode() + "\"}");
}

esp_err_t handleNetworks(httpd_req_t* req) {
	return sendJson(req, "200 OK", "{\"networks\":" + g_networksJson + "}");
}

// Kick off the live connect test; the loop task runs it, the page polls status.
esp_err_t handleVerify(httpd_req_t* req) {
	std::string body;
	if (!readBody(req, body))
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"read\"}");
	std::string ssid, pwd;
	formField(body, "ssid", ssid);
	formField(body, "password", pwd);
	if (ssid.empty() || ssid.size() > 32)
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"ssid\"}");
	if (!pwd.empty() && (pwd.size() < 8 || pwd.size() > 63))
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"password\"}");
	xSemaphoreTake(g_mx, portMAX_DELAY);
	g_vssid  = ssid;
	g_vpwd   = pwd;
	g_vstate = VerifyState::Testing;
	xSemaphoreGive(g_mx);
	return sendJson(req, "200 OK", "{\"ok\":true}");
}

esp_err_t handleVerifyStatus(httpd_req_t* req) {
	xSemaphoreTake(g_mx, portMAX_DELAY);
	VerifyState st = g_vstate;
	xSemaphoreGive(g_mx);
	const char* s = st == VerifyState::Ok ? "ok" : (st == VerifyState::Fail ? "fail" : "testing");
	return sendJson(req, "200 OK", std::string("{\"state\":\"") + s + "\"}");
}

esp_err_t handleFinish(httpd_req_t* req) {
	std::string body;
	if (!readBody(req, body))
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"read\"}");

	std::string ssid, pwd, code;
	formField(body, "ssid", ssid);
	formField(body, "password", pwd);
	formField(body, "code", code);

	if (ssid.empty() || ssid.size() > 32)
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"ssid\"}");
	if (!pwd.empty() && (pwd.size() < 8 || pwd.size() > 63))
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"password\"}");
	if (!validCode(code))
		return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"code\"}");

	xSemaphoreTake(g_mx, portMAX_DELAY);
	g_ssid = ssid;
	g_pwd  = pwd;
	g_code = code;
	g_fstate = FinishState::Pending;
	xSemaphoreGive(g_mx);

	// Wait for the loop task to persist + build the QR (SRP gen can take ~1-2 s).
	for (int i = 0; i < 600; ++i) {
		vTaskDelay(pdMS_TO_TICKS(20));
		xSemaphoreTake(g_mx, portMAX_DELAY);
		FinishState st = g_fstate;
		std::string qr = g_qr, nonce = g_nonce, err = g_ferr, cc = g_code, ip = g_verifyIp;
		xSemaphoreGive(g_mx);
		if (st == FinishState::Done)
			return sendJson(req, "200 OK", "{\"ok\":true,\"qr\":\"" + qr + "\",\"code\":\"" + cc +
			                                   "\",\"nonce\":\"" + nonce + "\",\"ip\":\"" + ip + "\"}");
		if (st == FinishState::Failed)
			return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"" + err + "\"}");
	}
	return sendJson(req, "200 OK", "{\"ok\":false,\"error\":\"timeout\"}");
}

esp_err_t handleReboot(httpd_req_t* req) {
	esp_err_t res = sendJson(req, "200 OK", "{\"ok\":true}");
	g_reboot = true;  // the run() loop flushes then restarts
	return res;
}

esp_err_t handleCatchAll(httpd_req_t* req) {
	std::string path(req->uri);
	size_t q = path.find('?');
	if (q != std::string::npos)
		path.resize(q);
	if (path == "/")
		path = "/onboard.html";
	const web_assets::Asset* a = findAsset(path);
	if (a)
		return sendAsset(req, a);
	// Unknown path: bounce to the portal so OS captive-portal detection opens it.
	std::string loc = "http://" + g_apIp + "/onboard.html";
	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", loc.c_str());
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	return httpd_resp_send(req, nullptr, 0);
}

void registerUri(const char* path, httpd_method_t method, esp_err_t (*fn)(httpd_req_t*)) {
	httpd_uri_t uri = {.uri = path, .method = method, .handler = fn, .user_ctx = nullptr};
	httpd_register_uri_handler(g_httpd, &uri);
}

// Loop task: STA-connects to test the creds, then drops STA so the SoftAP regains its
// channel. The portal client may briefly lose the AP during this.
void applyVerify() {
	std::string ssid, pwd;
	xSemaphoreTake(g_mx, portMAX_DELAY);
	ssid = g_vssid;
	pwd  = g_vpwd;
	xSemaphoreGive(g_mx);

	WiFi.begin(ssid.c_str(), pwd.empty() ? nullptr : pwd.c_str());
	uint64_t t0 = esp_timer_get_time();
	while (WiFi.status() != WL_CONNECTED && (esp_timer_get_time() - t0) < 15000000ULL) {
		esp_task_wdt_reset();
		delay(100);
	}
	bool ok = WiFi.status() == WL_CONNECTED;
	std::string ip;
	if (ok)
		ip = WiFi.localIP().toString().c_str();  // capture before dropping STA; the done screen shows it
	WiFi.disconnect(false);  // drop STA, keep the SoftAP up

	xSemaphoreTake(g_mx, portMAX_DELAY);
	g_verifyIp = ip;
	g_vstate = ok ? VerifyState::Ok : VerifyState::Fail;
	xSemaphoreGive(g_mx);
}

// Loop task: persists the creds + code, builds the QR payload.
void applyFinish() {
	std::string ssid, pwd, code;
	xSemaphoreTake(g_mx, portMAX_DELAY);
	ssid = g_ssid;
	pwd  = g_pwd;
	code = g_code;
	xSemaphoreGive(g_mx);

	homeSpan.setWifiCredentials(ssid.c_str(), pwd.c_str());
	homeSpan.setPairingCode(code.c_str());

	std::string nonce, err;
	runtime::AuthStore store;
	if (store.ok()) {
		if (!store.getNonce(nonce)) {  // first boot: mint the admin nonce the done screen shows
			uint8_t rb[9];
			esp_fill_random(rb, sizeof(rb));
			nonce = runtime::makeNonce(rb, sizeof(rb));
			store.setNonce(nonce);
		}
		store.setSetupCode(code);
	} else {
		err = "store";
	}

	HapQR qr;
	std::string payload = qr.get(static_cast<uint32_t>(strtoul(code.c_str(), nullptr, 10)),
	                             kQrSetupId, kBridgeCategory);

	xSemaphoreTake(g_mx, portMAX_DELAY);
	if (err.empty()) {
		g_qr     = payload;
		g_nonce  = nonce;
		g_fstate = FinishState::Done;
	} else {
		g_ferr   = err;
		g_fstate = FinishState::Failed;
	}
	xSemaphoreGive(g_mx);
}

}  // namespace

void run() {
	g_mx = xSemaphoreCreateMutex();
	initUI();  // paint the status LED here: begin() blocks in run(), so main's initUI() never runs

	WiFi.persistent(false);     // don't let the SoftAP config touch the WiFi-lib NVS store
	WiFi.setAutoReconnect(false);  // a failed verify must not retry in the background
	WiFi.mode(WIFI_AP_STA);     // AP for the portal, STA so the live verify can connect
	uint8_t mac[6] = {0};
	esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);  // efuse read: AP MAC isn't live until softAP() below
	char ssid[33];
	snprintf(ssid, sizeof(ssid), "%s%02X%02X", kApPrefix, mac[4], mac[5]);
	scanNetworks();     // must precede softAP (no client to drop during the sweep)
	WiFi.softAP(ssid);  // open network; trust is RF presence in the time-boxed window
	g_apIp = WiFi.softAPIP().toString().c_str();
	g_dns.start(53, "*", WiFi.softAPIP());  // captive: answer every query with the AP IP

	httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
	conf.server_port      = 80;
	conf.uri_match_fn     = httpd_uri_match_wildcard;
	conf.max_uri_handlers = 8;
	if (httpd_start(&g_httpd, &conf) == ESP_OK) {
		registerUri("/api/onboard/state", HTTP_GET, handleState);
		registerUri("/api/onboard/networks", HTTP_GET, handleNetworks);
		registerUri("/api/onboard/verify", HTTP_POST, handleVerify);
		registerUri("/api/onboard/verify-status", HTTP_GET, handleVerifyStatus);
		registerUri("/api/onboard/finish", HTTP_POST, handleFinish);
		registerUri("/api/onboard/reboot", HTTP_POST, handleReboot);
		registerUri("/*", HTTP_GET, handleCatchAll);  // last: wildcard fallback
	} else {
		Serial.println("onboard: httpd_start failed");
	}

	uint64_t startUs = esp_timer_get_time();
	while (!g_reboot && (esp_timer_get_time() - startUs) < kPortalTimeoutMs * 1000ULL) {
		g_dns.processNextRequest();
		esp_task_wdt_reset();  // begin() no longer feeds the watchdog; we own the task
		FinishState fst;
		VerifyState vst;
		xSemaphoreTake(g_mx, portMAX_DELAY);
		fst = g_fstate;
		vst = g_vstate;
		xSemaphoreGive(g_mx);
		if (vst == VerifyState::Testing)
			applyVerify();
		if (fst == FinishState::Pending)
			applyFinish();
		delay(20);
	}

	delay(200);  // let the final HTTP response flush before the radio drops
	ESP.restart();
}

}  // namespace onboarding
