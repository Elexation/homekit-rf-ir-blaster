#include "config_json.h"

#include <cstring>

#include <ArduinoJson.h>

namespace config {

static void readStr(JsonVariantConst v, std::string& dst) {
	const char* p = v.as<const char*>();  // null if the field is absent / not a string
	if (p) dst = p;
}

static void writeCodeFields(JsonObject jc, const StoredCode& code) {
	switch (code.kind) {
		case CodeKind::RF:
			jc["kind"]        = "rf";
			jc["freqMHz"]     = code.freqMHz;
			jc["rolling"]     = code.rolling;
			jc["frameRepeat"] = code.frameRepeat;
			break;
		case CodeKind::IR:
			jc["kind"]      = "ir";
			jc["carrierHz"] = code.carrierHz;
			break;
		case CodeKind::None:
			jc["kind"] = "none";
			break;
	}
	JsonArray p = jc["pulses"].to<JsonArray>();
	for (uint16_t v : code.pulses) p.add(v);
}

bool toJson(const Config& cfg, std::string& out) {
	JsonDocument doc;

	doc["schemaVersion"] = cfg.schemaVersion;

	JsonObject s = doc["settings"].to<JsonObject>();
	s["https"]               = cfg.settings.https;
	s["listenPort"]          = cfg.settings.listenPort;
	s["trustedProxy"]        = cfg.settings.trustedProxy;
	s["canonicalDomain"]     = cfg.settings.canonicalDomain;
	s["ledEnabled"]          = cfg.settings.ledEnabled;

	doc["nextDeviceId"] = cfg.nextDeviceId;

	JsonArray devs = doc["devices"].to<JsonArray>();
	for (const auto& d : cfg.devices) {
		JsonObject jd = devs.add<JsonObject>();
		jd["id"]      = d.id;
		jd["service"] = d.service;
		jd["name"]    = d.name;

		JsonObject cmds = jd["commands"].to<JsonObject>();
		for (const auto& slot : d.commands) {
			JsonObject jc = cmds[slot.name].to<JsonObject>();
			writeCodeFields(jc, slot.code);
			jc["repeatCount"]   = slot.repeatCount;
			jc["repeatDelayMs"] = slot.repeatDelayMs;
		}
	}

	const size_t n = measureJson(doc);
	if (n > MAX_CONFIG_BYTES) return false;

	out.resize(n + 1);
	const size_t written = serializeJson(doc, &out[0], out.size());
	out.resize(written);
	return true;
}

bool codeToJson(const StoredCode& code, std::string& out) {
	JsonDocument doc;
	writeCodeFields(doc.to<JsonObject>(), code);

	const size_t n = measureJson(doc);
	out.resize(n + 1);
	const size_t written = serializeJson(doc, &out[0], out.size());
	out.resize(written);
	return true;
}

bool fromJson(const char* data, size_t len, Config& out) {
	if (len > MAX_CONFIG_BYTES) return false;

	JsonDocument doc;
	const DeserializationError err = deserializeJson(
		doc, data, len, DeserializationOption::NestingLimit(NESTING_LIMIT));
	if (err) return false;

	out = Config{};

	out.schemaVersion = doc["schemaVersion"] | out.schemaVersion;

	JsonObjectConst s = doc["settings"].as<JsonObjectConst>();
	out.settings.https               = s["https"]               | out.settings.https;
	out.settings.listenPort          = s["listenPort"]          | out.settings.listenPort;
	out.settings.trustedProxy        = s["trustedProxy"]        | out.settings.trustedProxy;
	readStr(s["canonicalDomain"], out.settings.canonicalDomain);
	out.settings.ledEnabled          = s["ledEnabled"]          | out.settings.ledEnabled;

	out.nextDeviceId = doc["nextDeviceId"] | out.nextDeviceId;

	for (JsonVariantConst dv : doc["devices"].as<JsonArrayConst>()) {
		JsonObjectConst jd = dv.as<JsonObjectConst>();
		VirtualDevice d;
		d.id                  = jd["id"] | d.id;
		readStr(jd["service"], d.service);
		readStr(jd["name"], d.name);

		for (JsonPairConst kv : jd["commands"].as<JsonObjectConst>()) {
			CommandSlot slot;
			slot.name = kv.key().c_str();
			JsonObjectConst jc = kv.value().as<JsonObjectConst>();
			const char* kind = jc["kind"] | "none";
			if (std::strcmp(kind, "rf") == 0) {
				slot.code.kind        = CodeKind::RF;
				slot.code.freqMHz     = jc["freqMHz"] | slot.code.freqMHz;
				slot.code.rolling     = jc["rolling"] | slot.code.rolling;
				slot.code.frameRepeat = jc["frameRepeat"] | slot.code.frameRepeat;
			} else if (std::strcmp(kind, "ir") == 0) {
				slot.code.kind      = CodeKind::IR;
				slot.code.carrierHz = jc["carrierHz"] | slot.code.carrierHz;
			}
			for (JsonVariantConst pv : jc["pulses"].as<JsonArrayConst>())
				slot.code.pulses.push_back(pv.as<uint16_t>());
			slot.repeatCount   = jc["repeatCount"]   | slot.repeatCount;
			slot.repeatDelayMs = jc["repeatDelayMs"] | slot.repeatDelayMs;
			d.commands.push_back(std::move(slot));
		}
		out.devices.push_back(std::move(d));
	}
	return true;
}

}  // namespace config
