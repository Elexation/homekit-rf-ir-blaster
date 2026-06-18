#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "accessory_plan.h"
#include "config_codec.h"
#include "config_json.h"
#include "config_model.h"
#include "config_validate.h"
#include "memory_blob_store.h"
#include "settings_change.h"

using namespace config;

void setUp() {}
void tearDown() {}

static StoredCode makeRf(uint16_t freq, std::vector<uint16_t> pulses) {
	StoredCode c;
	c.kind = CodeKind::RF;
	c.freqMHz = freq;
	c.pulses = std::move(pulses);
	return c;
}

static StoredCode makeIr(uint16_t carrier, std::vector<uint16_t> pulses) {
	StoredCode c;
	c.kind = CodeKind::IR;
	c.carrierHz = carrier;
	c.pulses = std::move(pulses);
	return c;
}

static Config sampleConfig() {
	Config cfg;
	cfg.settings.https = true;
	cfg.settings.listenPort = 8443;
	cfg.settings.trustedProxy = true;
	cfg.settings.canonicalDomain = "blaster.local";
	cfg.settings.ledEnabled = false;  // non-default so the round-trip exercises it
	cfg.nextDeviceId = 4;

	VirtualDevice screen;
	screen.id = 2;
	screen.service = "WindowCovering";
	screen.name = "Projector Screen";
	screen.options.repeatCount = 1;
	screen.commands.push_back({ "up",   makeRf(315, { 300, 900, 300, 900 }) });
	screen.commands.push_back({ "stop", makeRf(315, { 600, 600, 600, 600 }) });
	screen.commands.push_back({ "down", makeRf(315, { 900, 300, 900, 300 }) });

	VirtualDevice projector;
	projector.id = 3;
	projector.service = "Switch";
	projector.name = "Projector";
	projector.options.repeatCount = 2;
	projector.commands.push_back({ "on", makeIr(38000, { 9000, 4500, 560, 560 }) });

	cfg.devices.push_back(std::move(screen));
	cfg.devices.push_back(std::move(projector));
	return cfg;
}

static void assertCodeEqual(const StoredCode& a, const StoredCode& b) {
	TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(a.kind), static_cast<uint8_t>(b.kind));
	TEST_ASSERT_EQUAL_UINT16(a.freqMHz, b.freqMHz);
	TEST_ASSERT_EQUAL_UINT16(a.carrierHz, b.carrierHz);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(a.rolling), static_cast<int>(b.rolling));
	TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(a.pulses.size()),
	                         static_cast<uint32_t>(b.pulses.size()));
	for (size_t i = 0; i < a.pulses.size(); ++i)
		TEST_ASSERT_EQUAL_UINT16(a.pulses[i], b.pulses[i]);
}

static void assertConfigEqual(const Config& a, const Config& b) {
	TEST_ASSERT_EQUAL_UINT16(a.schemaVersion, b.schemaVersion);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(a.settings.https), static_cast<int>(b.settings.https));
	TEST_ASSERT_EQUAL_UINT16(a.settings.listenPort, b.settings.listenPort);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(a.settings.trustedProxy),
	                      static_cast<int>(b.settings.trustedProxy));
	TEST_ASSERT_EQUAL_STRING(a.settings.canonicalDomain.c_str(),
	                         b.settings.canonicalDomain.c_str());
	TEST_ASSERT_EQUAL_INT(static_cast<int>(a.settings.ledEnabled),
	                      static_cast<int>(b.settings.ledEnabled));
	TEST_ASSERT_EQUAL_UINT16(a.nextDeviceId, b.nextDeviceId);
	TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(a.devices.size()),
	                         static_cast<uint32_t>(b.devices.size()));
	for (size_t i = 0; i < a.devices.size(); ++i) {
		const VirtualDevice& da = a.devices[i];
		const VirtualDevice& db = b.devices[i];
		TEST_ASSERT_EQUAL_UINT16(da.id, db.id);
		TEST_ASSERT_EQUAL_STRING(da.service.c_str(), db.service.c_str());
		TEST_ASSERT_EQUAL_STRING(da.name.c_str(), db.name.c_str());
		TEST_ASSERT_EQUAL_UINT8(da.options.repeatCount, db.options.repeatCount);
		TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(da.commands.size()),
		                         static_cast<uint32_t>(db.commands.size()));
		for (size_t j = 0; j < da.commands.size(); ++j) {
			TEST_ASSERT_EQUAL_STRING(da.commands[j].name.c_str(), db.commands[j].name.c_str());
			assertCodeEqual(da.commands[j].code, db.commands[j].code);
		}
	}
}

// Serialize -> persist -> load -> deserialize survives a full round trip.
static void test_roundtrip_save_load() {
	Config original = sampleConfig();
	MemoryBlobStore store;
	TEST_ASSERT_TRUE(save(store, original));

	DecodeResult r = load(store);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(DecodeStatus::Ok), static_cast<int>(r.status));
	TEST_ASSERT_FALSE(r.usedDefaults);
	assertConfigEqual(original, r.config);
}

// The JSON layer alone round-trips without the binary envelope.
static void test_json_roundtrip() {
	Config original = sampleConfig();
	std::string json;
	TEST_ASSERT_TRUE(toJson(original, json));

	Config parsed;
	TEST_ASSERT_TRUE(fromJson(json.c_str(), json.size(), parsed));
	assertConfigEqual(original, parsed);
}

// A flipped payload byte fails the CRC and falls back to defaults.
static void test_crc_corruption_yields_defaults() {
	std::vector<uint8_t> blob;
	TEST_ASSERT_TRUE(encode(sampleConfig(), blob));
	blob[BLOB_HEADER_SIZE] ^= 0xFF;

	DecodeResult r = decode(blob);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(DecodeStatus::BadCrc), static_cast<int>(r.status));
	TEST_ASSERT_TRUE(r.usedDefaults);
	assertConfigEqual(Config{}, r.config);
}

// A wrong magic marks the blob foreign and falls back to defaults.
static void test_magic_corruption_yields_defaults() {
	std::vector<uint8_t> blob;
	TEST_ASSERT_TRUE(encode(sampleConfig(), blob));
	blob[0] ^= 0xFF;

	DecodeResult r = decode(blob);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(DecodeStatus::BadMagic), static_cast<int>(r.status));
	TEST_ASSERT_TRUE(r.usedDefaults);
}

// A schema version the firmware does not understand falls back to defaults.
static void test_schema_version_mismatch_yields_defaults() {
	std::vector<uint8_t> blob;
	TEST_ASSERT_TRUE(encode(sampleConfig(), blob));
	blob[4] = 0x63;  // version low byte -> 99
	blob[5] = 0x00;

	DecodeResult r = decode(blob);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(DecodeStatus::BadVersion), static_cast<int>(r.status));
	TEST_ASSERT_TRUE(r.usedDefaults);
}

// Input past the byte ceiling is refused before parsing.
static void test_oversize_json_rejected() {
	std::string big(MAX_CONFIG_BYTES + 1, ' ');
	Config out;
	TEST_ASSERT_FALSE(fromJson(big.c_str(), big.size(), out));
}

// A structurally valid config too large to serialize is refused by encode.
static void test_encode_over_byte_ceiling_rejected() {
	Config cfg;
	cfg.nextDeviceId = 64;
	std::vector<uint16_t> bigPulses(MAX_PULSES, 12345);
	for (uint16_t i = 0; i < 10; ++i) {
		VirtualDevice d;
		d.id = static_cast<uint16_t>(i + 2);
		d.service = "Switch";
		d.name = "Device";
		d.commands.push_back({ "on", makeIr(38000, bigPulses) });
		cfg.devices.push_back(std::move(d));
	}
	std::vector<uint8_t> blob;
	TEST_ASSERT_FALSE(encode(cfg, blob));
}

// More devices than the cap is rejected by validate.
static void test_too_many_devices_rejected() {
	Config cfg;
	cfg.nextDeviceId = static_cast<uint16_t>(MAX_DEVICES + 5);
	for (size_t i = 0; i <= MAX_DEVICES; ++i) {  // MAX_DEVICES + 1 devices
		VirtualDevice d;
		d.id = static_cast<uint16_t>(i + 2);
		d.service = "Switch";
		cfg.devices.push_back(std::move(d));
	}
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::TooManyDevices),
	                      static_cast<int>(validate(cfg)));
}

// A pulse buffer past the cap is rejected by validate.
static void test_over_long_pulses_rejected() {
	Config cfg;
	cfg.nextDeviceId = 3;
	VirtualDevice d;
	d.id = 2;
	d.service = "Switch";
	d.commands.push_back({ "on", makeIr(38000, std::vector<uint16_t>(MAX_PULSES + 2, 100)) });
	cfg.devices.push_back(std::move(d));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::TooManyPulses),
	                      static_cast<int>(validate(cfg)));
}

// A device at a reserved id (0/1; aid 1 is the bridge) is rejected by validate.
static void test_reserved_device_id_rejected() {
	Config cfg;
	cfg.nextDeviceId = 3;
	VirtualDevice d;
	d.id = 1;  // collides with the bridge accessory
	d.service = "Switch";
	cfg.devices.push_back(std::move(d));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::ReservedDeviceId),
	                      static_cast<int>(validate(cfg)));

	cfg.devices.clear();
	VirtualDevice zero;
	zero.id = 0;
	zero.service = "Switch";
	cfg.devices.push_back(std::move(zero));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::ReservedDeviceId),
	                      static_cast<int>(validate(cfg)));
}

// JSON nested past the depth ceiling is rejected by the parser.
static void test_over_deep_nesting_rejected() {
	const char* deep = "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":1}}}}}}}";
	Config out;
	TEST_ASSERT_FALSE(fromJson(deep, std::strlen(deep), out));
}

// A rejected (store-full) write leaves the previously stored config intact.
static void test_store_full_retains_previous() {
	Config small;  // defaults, no devices
	std::vector<uint8_t> smallBlob;
	TEST_ASSERT_TRUE(encode(small, smallBlob));

	Config big = sampleConfig();
	std::vector<uint8_t> bigBlob;
	TEST_ASSERT_TRUE(encode(big, bigBlob));
	TEST_ASSERT_TRUE(bigBlob.size() > smallBlob.size());

	MemoryBlobStore store(smallBlob.size());  // only fits the small config
	TEST_ASSERT_TRUE(save(store, small));
	TEST_ASSERT_FALSE(save(store, big));      // rejected; store left untouched

	DecodeResult r = load(store);
	TEST_ASSERT_FALSE(r.usedDefaults);
	assertConfigEqual(small, r.config);
}

static VirtualDevice dev(uint16_t id, const std::string& service, const std::string& name) {
	VirtualDevice d;
	d.id = id;
	d.service = service;
	d.name = name;
	return d;
}

static bool containsAid(const std::vector<uint16_t>& v, uint16_t aid) {
	for (uint16_t x : v)
		if (x == aid)
			return true;
	return false;
}

static const PlannedAccessory* findAcc(const std::vector<PlannedAccessory>& v, uint16_t aid) {
	for (const auto& a : v)
		if (a.aid == aid)
			return &a;
	return nullptr;
}

// Each known service name maps to its type; unknown/empty/wrong-case -> Unknown.
static void test_service_type_from_string() {
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Switch),
	                      static_cast<int>(serviceTypeFromString("Switch")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::WindowCovering),
	                      static_cast<int>(serviceTypeFromString("WindowCovering")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Outlet),
	                      static_cast<int>(serviceTypeFromString("Outlet")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::LightBulb),
	                      static_cast<int>(serviceTypeFromString("LightBulb")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Fan),
	                      static_cast<int>(serviceTypeFromString("Fan")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Television),
	                      static_cast<int>(serviceTypeFromString("Television")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Unknown),
	                      static_cast<int>(serviceTypeFromString("Thermostat")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Unknown),
	                      static_cast<int>(serviceTypeFromString("")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Unknown),
	                      static_cast<int>(serviceTypeFromString("switch")));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Unknown),
	                      static_cast<int>(serviceTypeFromString("Lightbulb")));  // lowercase b
}

// Commands resolve to a borrowed code, with learned reflecting isLearned().
static void test_plan_resolves_commands_and_learned() {
	Config cfg;
	cfg.nextDeviceId = 3;
	VirtualDevice screen;
	screen.id = 2;
	screen.service = "WindowCovering";
	screen.name = "Screen";
	screen.commands.push_back({ "up",   makeRf(315, { 300, 900 }) });  // learned
	screen.commands.push_back({ "stop", StoredCode{} });               // unlearned
	cfg.devices.push_back(std::move(screen));

	PlanResult plan = planAccessories(cfg);
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(plan.accessories.size()));
	TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(plan.skipped.size()));

	const PlannedAccessory& a = plan.accessories[0];
	TEST_ASSERT_EQUAL_UINT16(2, a.aid);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::WindowCovering), static_cast<int>(a.type));
	TEST_ASSERT_EQUAL_STRING("Screen", a.name.c_str());
	TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(a.commands.size()));

	TEST_ASSERT_EQUAL_STRING("up", a.commands[0].name.c_str());
	TEST_ASSERT_TRUE(a.commands[0].learned);
	TEST_ASSERT_NOT_NULL(a.commands[0].code);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(CodeKind::RF), static_cast<int>(a.commands[0].code->kind));
	TEST_ASSERT_EQUAL_UINT16(300, a.commands[0].code->pulses[0]);

	TEST_ASSERT_EQUAL_STRING("stop", a.commands[1].name.c_str());
	TEST_ASSERT_FALSE(a.commands[1].learned);
	TEST_ASSERT_NOT_NULL(a.commands[1].code);  // pointer valid even when unlearned
	TEST_ASSERT_EQUAL_INT(static_cast<int>(CodeKind::None), static_cast<int>(a.commands[1].code->kind));
}

// An unrecognized service is dropped from the plan but reported in skipped.
static void test_plan_skips_unknown_service() {
	Config cfg;
	cfg.nextDeviceId = 4;
	cfg.devices.push_back(dev(2, "Switch", "Lamp"));
	cfg.devices.push_back(dev(3, "Thermostat", "Nest"));

	PlanResult plan = planAccessories(cfg);
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(plan.accessories.size()));
	TEST_ASSERT_EQUAL_UINT16(2, plan.accessories[0].aid);
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(plan.skipped.size()));
	TEST_ASSERT_EQUAL_UINT16(3, plan.skipped[0].id);
	TEST_ASSERT_EQUAL_STRING("Thermostat", plan.skipped[0].service.c_str());
}

// Diff sorts devices into added, removed, and updated; unchanged ones into none.
static void test_diff_add_remove_update() {
	Config oldCfg;
	oldCfg.nextDeviceId = 5;
	oldCfg.devices.push_back(dev(2, "Switch", "A"));  // unchanged
	oldCfg.devices.push_back(dev(3, "Switch", "B"));  // renamed
	oldCfg.devices.push_back(dev(4, "Switch", "C"));  // removed
	Config newCfg;
	newCfg.nextDeviceId = 6;
	newCfg.devices.push_back(dev(2, "Switch", "A"));
	newCfg.devices.push_back(dev(3, "Switch", "B renamed"));
	newCfg.devices.push_back(dev(5, "Switch", "E"));  // added

	PlanResult oldP = planAccessories(oldCfg);
	PlanResult newP = planAccessories(newCfg);
	PlanDiff d = diffPlans(oldP.accessories, newP.accessories);

	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toAdd.size()));
	TEST_ASSERT_EQUAL_UINT16(5, d.toAdd[0].aid);
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toRemove.size()));
	TEST_ASSERT_TRUE(containsAid(d.toRemove, 4));
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toUpdate.size()));
	TEST_ASSERT_EQUAL_UINT16(3, d.toUpdate[0].aid);
	TEST_ASSERT_EQUAL_STRING("B renamed", d.toUpdate[0].name.c_str());
}

// A same-aid service-type change becomes remove + add, never an update.
static void test_diff_type_change_is_remove_add() {
	Config oldCfg;
	oldCfg.nextDeviceId = 3;
	oldCfg.devices.push_back(dev(2, "Switch", "X"));
	Config newCfg;
	newCfg.nextDeviceId = 3;
	newCfg.devices.push_back(dev(2, "Fan", "X"));

	PlanResult oldP = planAccessories(oldCfg);
	PlanResult newP = planAccessories(newCfg);
	PlanDiff d = diffPlans(oldP.accessories, newP.accessories);

	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toRemove.size()));
	TEST_ASSERT_TRUE(containsAid(d.toRemove, 2));
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toAdd.size()));
	const PlannedAccessory* added = findAcc(d.toAdd, 2);
	TEST_ASSERT_NOT_NULL(added);
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ServiceType::Fan), static_cast<int>(added->type));
	TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(d.toUpdate.size()));
}

// Deleting an id and adding another leaves surviving aids stable, and the new
// device takes the next id rather than reusing the freed one.
static void test_diff_id_stability_delete_add() {
	Config oldCfg;
	oldCfg.nextDeviceId = 4;
	oldCfg.devices.push_back(dev(2, "Switch", "First"));
	oldCfg.devices.push_back(dev(3, "WindowCovering", "Second"));
	Config newCfg;
	newCfg.nextDeviceId = 5;
	newCfg.devices.push_back(dev(3, "WindowCovering", "Second"));  // stable
	newCfg.devices.push_back(dev(4, "Switch", "Third"));           // new id, not reused 2

	PlanResult oldP = planAccessories(oldCfg);
	PlanResult newP = planAccessories(newCfg);
	PlanDiff d = diffPlans(oldP.accessories, newP.accessories);

	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toRemove.size()));
	TEST_ASSERT_TRUE(containsAid(d.toRemove, 2));
	TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(d.toAdd.size()));
	TEST_ASSERT_EQUAL_UINT16(4, d.toAdd[0].aid);
	TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(d.toUpdate.size()));
	TEST_ASSERT_NULL(findAcc(d.toAdd, 3));
}

// Diffing a plan against itself yields no changes.
static void test_diff_identical_plans_empty() {
	Config cfg;
	cfg.nextDeviceId = 4;
	cfg.devices.push_back(dev(2, "Switch", "A"));
	cfg.devices.push_back(dev(3, "Outlet", "B"));

	PlanResult p = planAccessories(cfg);
	PlanDiff d = diffPlans(p.accessories, p.accessories);
	TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(d.toAdd.size()));
	TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(d.toRemove.size()));
	TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(d.toUpdate.size()));
}

// Port 0 and the OTA/mDNS/HAP reserved ports are rejected.
static void test_listen_port_reserved_rejected() {
	Config cfg = sampleConfig();
	cfg.settings.listenPort = 0;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::BadListenPort),
	                      static_cast<int>(validate(cfg)));
	cfg.settings.listenPort = OTA_PORT;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::ReservedListenPort),
	                      static_cast<int>(validate(cfg)));
	cfg.settings.listenPort = MDNS_PORT;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::ReservedListenPort),
	                      static_cast<int>(validate(cfg)));
	cfg.settings.listenPort = HAP_PORT;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::ReservedListenPort),
	                      static_cast<int>(validate(cfg)));
}

// Port 80 is a valid listen port now that HomeKit is moved off it.
static void test_listen_port_80_allowed() {
	Config cfg = sampleConfig();
	cfg.settings.listenPort = 80;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::Ok),
	                      static_cast<int>(validate(cfg)));
}

// An empty domain is always allowed: it is optional and means "no canonical host".
static void test_empty_domain_allowed() {
	Config cfg = sampleConfig();
	cfg.settings.canonicalDomain = "";

	cfg.settings.https = true;
	cfg.settings.trustedProxy = true;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::Ok),
	                      static_cast<int>(validate(cfg)));

	cfg.settings.https = false;
	cfg.settings.trustedProxy = false;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::Ok),
	                      static_cast<int>(validate(cfg)));
}

// Hostnames, a single label, an IPv4 literal, and a trailing dot all pass.
static void test_valid_domains_accepted() {
	Config cfg = sampleConfig();
	cfg.settings.trustedProxy = true;
	auto ok = [&](const char* d) {
		cfg.settings.canonicalDomain = d;
		return validate(cfg) == ValidateError::Ok;
	};
	TEST_ASSERT_TRUE(ok("blaster.local"));
	TEST_ASSERT_TRUE(ok("blaster"));
	TEST_ASSERT_TRUE(ok("192.168.1.50"));
	TEST_ASSERT_TRUE(ok("blaster.local."));
}

// URL punctuation, a leading dash, a space, and an over-long label are rejected.
static void test_bad_domains_rejected() {
	Config cfg = sampleConfig();
	cfg.settings.trustedProxy = true;
	auto bad = [&](const std::string& d) {
		cfg.settings.canonicalDomain = d;
		return validate(cfg) == ValidateError::BadCanonicalDomain;
	};
	TEST_ASSERT_TRUE(bad("https://x"));
	TEST_ASSERT_TRUE(bad("x/admin"));
	TEST_ASSERT_TRUE(bad("x:8443"));
	TEST_ASSERT_TRUE(bad("-bad"));
	TEST_ASSERT_TRUE(bad("a b"));
	TEST_ASSERT_TRUE(bad(std::string(MAX_LABEL_LEN + 1, 'a')));
}

// A domain past MAX_DOMAIN_LEN trips the length check, not the format check.
static void test_domain_too_long_rejected() {
	Config cfg = sampleConfig();
	cfg.settings.trustedProxy = true;
	std::string longDomain;
	while (longDomain.size() <= MAX_DOMAIN_LEN) {
		if (!longDomain.empty())
			longDomain += '.';
		longDomain += std::string(50, 'a');  // valid 50-char labels; only length trips
	}
	TEST_ASSERT_TRUE(longDomain.size() > MAX_DOMAIN_LEN);
	cfg.settings.canonicalDomain = longDomain;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ValidateError::DomainTooLong),
	                      static_cast<int>(validate(cfg)));
}

// Identical settings classify as Live; the wrapper agrees.
static void test_classify_identical_is_live() {
	Settings a;
	Settings b;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplyKind::Live),
	                      static_cast<int>(classifyChange(a, b)));
	TEST_ASSERT_FALSE(requiresRestart(a, b));
}

// Each of the four Settings fields, changed alone, requires a restart.
static void test_classify_each_field_requires_restart() {
	Settings base;

	Settings p = base; p.listenPort = 8080;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplyKind::RequiresRestart),
	                      static_cast<int>(classifyChange(base, p)));
	TEST_ASSERT_TRUE(requiresRestart(base, p));

	Settings d = base; d.canonicalDomain = "other.local";
	TEST_ASSERT_TRUE(requiresRestart(base, d));

	Settings h = base; h.https = !base.https;
	TEST_ASSERT_TRUE(requiresRestart(base, h));

	Settings tp = base; tp.trustedProxy = !base.trustedProxy;
	TEST_ASSERT_TRUE(requiresRestart(base, tp));
}

// A ledEnabled-only change is Live, not a restart.
static void test_classify_led_only_is_live() {
	Settings base;
	Settings led = base; led.ledEnabled = !base.ledEnabled;
	TEST_ASSERT_EQUAL_INT(static_cast<int>(ApplyKind::Live),
	                      static_cast<int>(classifyChange(base, led)));
	TEST_ASSERT_FALSE(requiresRestart(base, led));
}

int main(int, char**) {
	UNITY_BEGIN();
	RUN_TEST(test_roundtrip_save_load);
	RUN_TEST(test_json_roundtrip);
	RUN_TEST(test_crc_corruption_yields_defaults);
	RUN_TEST(test_magic_corruption_yields_defaults);
	RUN_TEST(test_schema_version_mismatch_yields_defaults);
	RUN_TEST(test_oversize_json_rejected);
	RUN_TEST(test_encode_over_byte_ceiling_rejected);
	RUN_TEST(test_too_many_devices_rejected);
	RUN_TEST(test_over_long_pulses_rejected);
	RUN_TEST(test_reserved_device_id_rejected);
	RUN_TEST(test_over_deep_nesting_rejected);
	RUN_TEST(test_store_full_retains_previous);
	RUN_TEST(test_service_type_from_string);
	RUN_TEST(test_plan_resolves_commands_and_learned);
	RUN_TEST(test_plan_skips_unknown_service);
	RUN_TEST(test_diff_add_remove_update);
	RUN_TEST(test_diff_type_change_is_remove_add);
	RUN_TEST(test_diff_id_stability_delete_add);
	RUN_TEST(test_diff_identical_plans_empty);
	RUN_TEST(test_listen_port_reserved_rejected);
	RUN_TEST(test_listen_port_80_allowed);
	RUN_TEST(test_empty_domain_allowed);
	RUN_TEST(test_valid_domains_accepted);
	RUN_TEST(test_bad_domains_rejected);
	RUN_TEST(test_domain_too_long_rejected);
	RUN_TEST(test_classify_identical_is_live);
	RUN_TEST(test_classify_each_field_requires_restart);
	RUN_TEST(test_classify_led_only_is_live);
	return UNITY_END();
}
