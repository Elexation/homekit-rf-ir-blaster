#pragma once

#include <esp_http_server.h>

#include <string>

#include "config_model.h"

namespace web {

// Init the auth layer: per-boot session secret, credential store, and (first boot) a setup
// nonce printed to serial. Call once before registering handlers.
void authBegin(const config::Settings& settings);

// Register the auth endpoints; before the catch-all so the wildcard doesn't swallow them.
void registerAuthApi(httpd_handle_t server);

// Page-gating verdict, evaluated in the catch-all.
enum class GateResult {
	Allow,             // serve the requested asset
	RedirectSetup,     // no credential yet -> /setup.html
	RedirectLogin,     // credential set, no valid session -> /login.html
	RedirectDashboard, // already authed but asked for /login.html or /setup.html
};

// Gate the resolved path: only top-level .html is gated; assets always pass so the
// login/setup pages can render.
GateResult gate(httpd_req_t* req, const std::string& path);

// Valid session cookie? For API endpoints (the page gate only covers .html loads).
bool sessionValid(httpd_req_t* req);

// Verify a CSRF token against the session binding + CSRF cookie. For state-changing calls.
bool csrfValid(httpd_req_t* req, const std::string& token);

}  // namespace web
