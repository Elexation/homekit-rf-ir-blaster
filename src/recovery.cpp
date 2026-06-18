#include "recovery.h"

#include <Arduino.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "boot_recovery.h"
#include "ui.h"

namespace recovery {

namespace {

constexpr char NVS_NAMESPACE[] = "rfirboot";  // separate from config + auth namespaces
constexpr char KEY_COUNT[]     = "count";     // consecutive-boot counter
constexpr char KEY_SAFEREQ[]   = "safereq";   // one-shot safe-mode request from a long-press

constexpr int      kBootPin       = 0;      // onboard BOOT button, active low
constexpr uint32_t kSafeHoldMs    = 5000;
constexpr uint32_t kFactoryHoldMs = 10000;

// safe mode forces plain HTTP so the page stays reachable
constexpr bool     kSafeHttps      = false;
constexpr uint16_t kSafeListenPort = 80;

nvs_handle_t g_nvs       = 0;
bool         g_ok        = false;
bool         g_safeMode  = false;
uint32_t     g_clearAtMs = 0;
bool         g_cleared   = false;

uint8_t readU8(const char* key, uint8_t fallback) {
	uint8_t v = fallback;
	if (!g_ok || nvs_get_u8(g_nvs, key, &v) != ESP_OK)
		return fallback;
	return v;
}

void writeU8(const char* key, uint8_t v) {
	if (!g_ok)
		return;
	nvs_set_u8(g_nvs, key, v);
	nvs_commit(g_nvs);
}

void eraseKey(const char* key) {
	if (!g_ok)
		return;
	nvs_erase_key(g_nvs, key);
	nvs_commit(g_nvs);
}

void applySafeDefaults(config::Settings& s) {
	s.https           = kSafeHttps;
	s.listenPort      = kSafeListenPort;
	s.trustedProxy    = false;
	s.canonicalDomain = "";
}

void factoryReset() {
	delay(150);
	nvs_flash_erase();  // wipes config, auth, HomeKit pairing, and WiFi creds
	ESP.restart();
}

void requestSafeModeReboot() {
	writeU8(KEY_SAFEREQ, 1);  // honored once on the next boot
	delay(150);
	ESP.restart();
}

void pollButton(uint32_t now) {
	static bool     pressed    = false;
	static uint32_t pressStart = 0;
	bool            down       = digitalRead(kBootPin) == LOW;

	if (down && !pressed) {
		pressed    = true;
		pressStart = now;
	} else if (down && pressed) {
		uint32_t held = now - pressStart;
		if (held >= kFactoryHoldMs)
			setUiState(UiState::FactoryArmed);
		else if (held >= kSafeHoldMs)
			setUiState(UiState::SafeModeArmed);
	} else if (!down && pressed) {
		uint32_t held = now - pressStart;
		pressed = false;
		if (held >= kFactoryHoldMs)
			factoryReset();
		else if (held >= kSafeHoldMs)
			requestSafeModeReboot();
		else
			setUiState(g_safeMode ? UiState::SafeMode : UiState::Normal);
	}
}

}  // namespace

void begin(config::Settings& settings) {
	// Static-init order vs HomeSpan is undefined; init the partition ourselves (idempotent).
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		nvs_flash_init();
	}
	g_ok = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs) == ESP_OK;

	pinMode(kBootPin, INPUT_PULLUP);

	uint8_t  count   = readU8(KEY_COUNT, 0);
	uint8_t  safeReq = readU8(KEY_SAFEREQ, 0);
	uint32_t now     = millis();
	g_clearAtMs      = now + runtime::kStableRunMs;
	g_cleared        = false;

	// Count only power-cycles/crashes toward the 3x gesture, not software restarts
	bool counterSafe = false;
	if (esp_reset_reason() != ESP_RST_SW) {
		runtime::BootDecision d = runtime::evaluateBoot(count, now);
		writeU8(KEY_COUNT, d.newCount);
		g_clearAtMs = d.clearAtMs;
		counterSafe = d.enterSafeMode;
	}

	g_safeMode = counterSafe || safeReq != 0;
	if (safeReq != 0)
		eraseKey(KEY_SAFEREQ);  // one-shot: consume the request

	if (g_safeMode) {
		writeU8(KEY_COUNT, 0);  // consume the boot-loop signal so a Save & Restart returns clean
		applySafeDefaults(settings);
		setUiState(UiState::SafeMode);
	}
}

bool inSafeMode() {
	return g_safeMode;
}

void poll() {
	uint32_t now = millis();
	if (!g_cleared && runtime::shouldClearCounter(now, g_clearAtMs)) {
		writeU8(KEY_COUNT, 0);
		g_cleared = true;
	}
	pollButton(now);
}

}  // namespace recovery
