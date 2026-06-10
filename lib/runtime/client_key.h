#pragma once

#include <cstdint>
#include <string>

namespace runtime {

// Resolve the throttle key (an IPv4 in host byte order) for the LoginThrottle. Pure: no
// socket access. Forwarded headers are client-spoofable, so they override the socket peer
// only when trustedProxy is set; otherwise peerIpv4 wins. Device seam: peerIpv4 from
// getpeername(), the headers from X-Real-IP / X-Forwarded-For (empty if absent).
uint32_t deriveClientKey(uint32_t peerIpv4, bool trustedProxy,
                         const std::string& xRealIp, const std::string& xForwardedFor);

}  // namespace runtime
