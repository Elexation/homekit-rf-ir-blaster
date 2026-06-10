#pragma once

#include <string>

#include "config_model.h"

namespace runtime {

enum class Scheme { Http, Https };
enum class Action { Serve, Redirect, Reject };

struct Decision {
	Action      action;
	std::string location;  // set only when action == Redirect
};

// A config-web request reduced to what the redirect/HTTPS policy needs; the device fills
// these from the request socket and headers, echoing `target` verbatim into any redirect.
struct Request {
	Scheme      transportScheme = Scheme::Http;  // listener that received it
	std::string forwardedProto;                  // X-Forwarded-Proto, empty if absent
	std::string host;                            // Host header, may include ":port"
	std::string target;                          // path + query, e.g. "/settings?x=1"
	bool        isLoopback = false;              // peer is 127.0.0.0/8 or ::1
};

// Apply the HTTPS / redirect / canonical settings. The canonical-host redirect is for
// origin/cert consistency only, never an access boundary (it fires after the connection
// reached the device, and Host is client-controlled). Loopback bypasses all redirects.
Decision evaluate(const Request& req, const config::Settings& settings);

}  // namespace runtime
