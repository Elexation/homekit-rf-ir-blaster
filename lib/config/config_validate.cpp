#include "config_validate.h"

#include <set>

namespace config {

// Strict dotted-quad: exactly four numeric octets, each 0..255, no trailing dot.
static bool parseIPv4(const std::string& s) {
	int parts = 0;
	size_t i = 0, n = s.size();
	while (i < n) {
		int val = 0, digits = 0;
		while (i < n && s[i] >= '0' && s[i] <= '9') {
			val = val * 10 + (s[i] - '0');
			if (++digits > 3)
				return false;
			++i;
		}
		if (digits == 0 || val > 255)
			return false;
		++parts;
		if (i < n) {
			if (s[i] != '.')
				return false;
			if (++i == n)
				return false;  // no trailing dot
		}
	}
	return parts == 4;
}

// Hostname: dot-separated labels of [A-Za-z0-9-], each 1..MAX_LABEL_LEN, no
// leading/trailing '-', one optional trailing dot. No TLD required.
static bool isValidHostname(const std::string& s) {
	std::string h = s;
	if (!h.empty() && h.back() == '.')
		h.pop_back();  // one optional trailing dot
	if (h.empty())
		return false;
	size_t start = 0;
	for (size_t i = 0; i <= h.size(); ++i) {
		if (i == h.size() || h[i] == '.') {
			size_t len = i - start;
			if (len == 0 || len > MAX_LABEL_LEN)
				return false;
			if (h[start] == '-' || h[i - 1] == '-')
				return false;
			for (size_t j = start; j < i; ++j) {
				char c = h[j];
				bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				          (c >= '0' && c <= '9') || c == '-';
				if (!ok)
					return false;
			}
			start = i + 1;
		}
	}
	return true;
}

// Accept a hostname or an IPv4 literal; reject scheme, path, query, fragment,
// port, and whitespace outright (the ':' check also rules out '://').
static bool isValidHostOrIp(const std::string& s) {
	if (s.empty())
		return false;
	for (char c : s) {
		if (c == '/' || c == '?' || c == '#' || c == ':' ||
		    c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
			return false;
	}
	return parseIPv4(s) || isValidHostname(s);
}

ValidateError validate(const Config& cfg) {
	if (cfg.devices.size() > MAX_DEVICES)
		return ValidateError::TooManyDevices;

	if (cfg.settings.listenPort == 0)
		return ValidateError::BadListenPort;
	if (cfg.settings.listenPort == OTA_PORT || cfg.settings.listenPort == MDNS_PORT)
		return ValidateError::ReservedListenPort;

	// canonicalDomain only matters when it is used to build an origin.
	if (cfg.settings.httpToHttpsRedirect || cfg.settings.trustedProxy) {
		const std::string& domain = cfg.settings.canonicalDomain;
		if (domain.empty())
			return ValidateError::EmptyCanonicalDomain;
		if (domain.size() > MAX_DOMAIN_LEN)
			return ValidateError::DomainTooLong;
		if (!isValidHostOrIp(domain))
			return ValidateError::BadCanonicalDomain;
	}

	std::set<uint16_t> seenIds;
	for (const auto& d : cfg.devices) {
		if (!seenIds.insert(d.id).second)
			return ValidateError::DuplicateDeviceId;
		if (d.id < 2)
			return ValidateError::ReservedDeviceId;  // 0/1 reserved; aid 1 is the bridge
		if (d.id >= cfg.nextDeviceId)
			return ValidateError::NextIdNotMonotonic;
		if (d.service.empty())
			return ValidateError::BadService;

		for (const auto& slot : d.commands) {
			const StoredCode& code = slot.code;
			if (code.kind == CodeKind::None)
				continue;  // unlearned slot; allowed
			if (code.pulses.empty())
				return ValidateError::EmptyLearnedCode;
			if (code.pulses.size() % 2 != 0)
				return ValidateError::OddPulseCount;
			if (code.pulses.size() > MAX_PULSES)
				return ValidateError::TooManyPulses;
			if (code.kind == CodeKind::RF) {
				if (code.freqMHz != 315 && code.freqMHz != 433)
					return ValidateError::BadRfFreq;
			} else if (code.kind == CodeKind::IR) {
				if (code.carrierHz < 20000 || code.carrierHz > 60000)
					return ValidateError::BadIrCarrier;
			}
		}
	}
	return ValidateError::Ok;
}

}  // namespace config
