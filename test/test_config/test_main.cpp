#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "config_codec.h"
#include "config_json.h"
#include "config_model.h"
#include "config_validate.h"
#include "memory_blob_store.h"

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
	TEST_ASSERT_EQUAL_INT(static_cast<int>(a.settings.httpToHttpsRedirect),
	                      static_cast<int>(b.settings.httpToHttpsRedirect));
	TEST_ASSERT_EQUAL_INT(static_cast<int>(a.settings.requireHttps),
	                      static_cast<int>(b.settings.requireHttps));
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
	RUN_TEST(test_over_deep_nesting_rejected);
	RUN_TEST(test_store_full_retains_previous);
	return UNITY_END();
}
