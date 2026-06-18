#include "web_config_api.h"

#include "HomeSpan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "accessory_builder.h"
#include "config_codec.h"
#include "config_json.h"
#include "config_validate.h"
#include "nvs_blob_store.h"
#include "settings_change.h"
#include "ui.h"
#include "web_auth.h"

namespace web {

namespace {

// Config payload ceiling plus slack; rev rides in the query string, not the body.
constexpr size_t kMaxBody = config::MAX_CONFIG_BYTES + 2048;

// HomeSpan stores only the SRP verifier, not the plaintext setup code; show a placeholder.
constexpr char kSetupCodePlaceholder[] = "Not available";

config::Config        g_config;          // authoritative copy; touched only on the httpd task
uint32_t              g_rev = 1;          // bumped per successful write; RAM-only, resets on reboot
config::NvsBlobStore* g_store = nullptr;

// Handoff to the loop task: writes set a pending action, pollConfigApply drains it.
enum class Pending { None, Apply, Restart, Factory };
SemaphoreHandle_t g_lock        = nullptr;
Pending           g_pending     = Pending::None;
config::Config    g_pendingCfg;           // valid only when g_pending == Apply

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

// Session gate for every API call; the page gate only covers .html loads.
bool requireSession(httpd_req_t* req, esp_err_t& err) {
	if (sessionValid(req))
		return true;
	err = sendJson(req, "401 Unauthorized", "{\"ok\":false,\"error\":\"auth\"}");
	return false;
}

// CSRF gate for state-changing calls; token in the X-CSRF-Token header, double-submit
// cookie sent by the browser.
bool requireCsrf(httpd_req_t* req, esp_err_t& err) {
	if (csrfValid(req, headerValue(req, "X-CSRF-Token")))
		return true;
	err = sendJson(req, "403 Forbidden", "{\"ok\":false,\"error\":\"forbidden\"}");
	return false;
}

uint32_t queryRev(httpd_req_t* req) {
	char q[64];
	if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK)
		return 0xFFFFFFFFu;  // no query -> never matches g_rev -> treated as conflict
	char val[16];
	if (httpd_query_key_value(q, "rev", val, sizeof(val)) != ESP_OK)
		return 0xFFFFFFFFu;
	return static_cast<uint32_t>(strtoul(val, nullptr, 10));
}

void enqueue(Pending action) {
	if (!g_lock)
		return;
	xSemaphoreTake(g_lock, portMAX_DELAY);
	if (action == Pending::Apply)
		g_pendingCfg = g_config;
	g_pending = action;
	xSemaphoreGive(g_lock);
}

const char* pairingText() {
	switch (homeSpan.getStatus().first) {
		case HS_PAIRED:
		case HS_CONNECTED:       return "Paired";
		case HS_WIFI_NEEDED:
		case HS_WIFI_CONNECTING: return "Connecting";
		default:                 return "Not paired";
	}
}

// --- handlers ---

esp_err_t handleGetConfig(httpd_req_t* req) {
	esp_err_t err;
	if (!requireSession(req, err))
		return err;
	std::string cfg;
	if (!config::toJson(g_config, cfg))
		return sendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"encode\"}");
	std::string body = "{\"config\":" + cfg + ",\"rev\":" + std::to_string(g_rev) + "}";
	return sendJson(req, "200 OK", body);
}

esp_err_t handleGetStatus(httpd_req_t* req) {
	esp_err_t err;
	if (!requireSession(req, err))
		return err;
	std::string ip   = WiFi.localIP().toString().c_str();
	std::string wifi = std::to_string(WiFi.RSSI()) + " dBm";
	std::string body = "{\"ip\":\"" + ip + "\",\"wifi\":\"" + wifi + "\",\"setupCode\":\"" +
	                   kSetupCodePlaceholder + "\",\"pairing\":\"" + pairingText() + "\"}";
	return sendJson(req, "200 OK", body);
}

// Whole-config write: rev guard, then the host-tested parse/validate/persist pipeline,
// then a live apply or (if a network setting changed) an atomic persist-then-restart.
// The httpd task never mutates the HomeSpan database; it enqueues for the loop task.
esp_err_t handlePostConfig(httpd_req_t* req) {
	esp_err_t err;
	if (!requireSession(req, err))
		return err;

	std::string body;
	if (!readBody(req, body))  // drain the body before any early-return keeps keep-alive in sync
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"size\"}");

	if (!requireCsrf(req, err))
		return err;

	if (queryRev(req) != g_rev)
		return sendJson(req, "200 OK",
		                "{\"conflict\":true,\"rev\":" + std::to_string(g_rev) + "}");

	config::Config incoming;
	if (!config::fromJson(body.c_str(), body.size(), incoming))
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"parse\"}");
	if (config::validate(incoming) != config::ValidateError::Ok)
		return sendJson(req, "400 Bad Request", "{\"ok\":false,\"error\":\"invalid\"}");
	if (!config::save(*g_store, incoming))
		return sendJson(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"store\"}");

	bool restart = config::requiresRestart(g_config.settings, incoming.settings);
	g_config = std::move(incoming);
	uint32_t newRev = ++g_rev;

	std::string cfg;
	config::toJson(g_config, cfg);
	std::string out = "{\"conflict\":false,\"rev\":" + std::to_string(newRev) +
	                  ",\"config\":" + cfg + "}";
	esp_err_t res = sendJson(req, "200 OK", out);  // flush the response before acting
	enqueue(restart ? Pending::Restart : Pending::Apply);
	return res;
}

esp_err_t handleFactoryReset(httpd_req_t* req) {
	esp_err_t err;
	if (!requireSession(req, err) || !requireCsrf(req, err))
		return err;
	esp_err_t res = sendJson(req, "200 OK", "{\"ok\":true}");
	enqueue(Pending::Factory);
	return res;
}

// Capture needs the radios (not yet wired); degrade cleanly.
esp_err_t handleLearnStart(httpd_req_t* req) {
	esp_err_t err;
	if (!requireSession(req, err))
		return err;
	return sendJson(req, "200 OK", "{\"ok\":false,\"reason\":\"unavailable\"}");
}

esp_err_t handleLearnCancel(httpd_req_t* req) {
	esp_err_t err;
	if (!requireSession(req, err))
		return err;
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

}  // namespace

void configApiBegin(const config::Config& cfg) {
	g_config = cfg;
	g_rev    = 1;
	g_store  = new config::NvsBlobStore();
	g_lock   = xSemaphoreCreateMutex();
}

void registerConfigApi(httpd_handle_t server) {
	registerUri(server, "/api/config", HTTP_GET, handleGetConfig);
	registerUri(server, "/api/status", HTTP_GET, handleGetStatus);
	registerUri(server, "/api/config", HTTP_POST, handlePostConfig);
	registerUri(server, "/api/factory-reset", HTTP_POST, handleFactoryReset);
	registerUri(server, "/api/learn/start", HTTP_GET, handleLearnStart);
	registerUri(server, "/api/learn/cancel", HTTP_POST, handleLearnCancel);
}

void pollConfigApply() {
	if (!g_lock)
		return;
	Pending        action = Pending::None;
	config::Config cfg;
	if (xSemaphoreTake(g_lock, 0) == pdTRUE) {
		action = g_pending;
		if (action == Pending::Apply)
			cfg = std::move(g_pendingCfg);
		g_pending = Pending::None;
		xSemaphoreGive(g_lock);
	}
	switch (action) {
		case Pending::Apply:
			applyConfigChange(cfg);
			setLedEnabled(cfg.settings.ledEnabled);  // live display pref; no restart needed
			break;
		case Pending::Restart:
			delay(150);  // let the HTTP response flush before the socket drops
			ESP.restart();
			break;
		case Pending::Factory:
			delay(150);
			nvs_flash_erase();  // wipes config, auth, HomeKit pairing, and WiFi creds
			ESP.restart();
			break;
		case Pending::None:
			break;
	}
}

}  // namespace web
