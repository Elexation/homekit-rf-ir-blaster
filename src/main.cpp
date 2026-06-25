#include "HomeSpan.h"
#include "SpanRollback.h"  // hand OTA mark-valid to the sketch so the bootloader can auto-revert an early-boot crash

#include "radios.h"
#include "ir.h"
#include "ui.h"
#include "accessory_builder.h"
#include "onboarding.h"
#include "recovery.h"
#include "web_server.h"
#include "web_config_api.h"
#include "auth_store.h"

#include "config_codec.h"
#include "config_model.h"
#include "nvs_blob_store.h"

static void markOtaImageGood() {
	homeSpan.markSketchOK();
	Serial.println("ota: image validated");
}

void setup() {
	Serial.begin(115200);

	homeSpan.setPortNum(config::HAP_PORT);  // move HAP off 80/443 for the config server
	homeSpan.setQRID(onboarding::kQrSetupId);    // advertised Setup ID must match the wizard's QR
	homeSpan.setApFunction(onboarding::run);     // replace the built-in AP with the onboarding portal
	homeSpan.enableAutoStartAP();
	homeSpan.enableWatchdog(60);
	homeSpan.setConnectionCallback(web::start);  // start the config server on WiFi connect (runs once)
	homeSpan.setPollingCallback(markOtaImageGood);  // mark the OTA image good once the first poll proves boot succeeded

	// Per-device password so OTA never ships HomeSpan's public default; created on first boot.
	runtime::AuthStore otaStore;
	std::string otaPw;
	if (!otaStore.getOtaPassword(otaPw)) {
		otaPw = runtime::makeOtaPassword();
		otaStore.setOtaPassword(otaPw);
	}
	homeSpan.enableOTA(otaPw.c_str());

	homeSpan.begin(Category::Bridges, "RF-IR Blaster");

	initRadios();
	initIR();
	initUI();

	config::NvsBlobStore store;
	config::DecodeResult res = config::load(store);
	Serial.printf("config: status=%d usedDefaults=%d devices=%u\n",
	              (int)res.status, res.usedDefaults, (unsigned)res.config.devices.size());

	// Recovery overrides a COPY of settings in safe mode; the config API still gets the real ones.
	config::Settings effective = res.config.settings;
	recovery::begin(effective);

	setLedEnabled(res.config.settings.ledEnabled);  // not a network setting, so use the real value, not the safe-mode copy

	web::begin(effective);            // stash settings before the WiFi callback can fire
	web::configApiBegin(res.config);  // seed the config API before the server starts
	buildAccessories(res.config);
}

// Recover HomeSpan's "No Response" zombie: prolonged HS_PAIRED = mDNS died, reboot re-advertises.
static void checkHomeKitLiveness() {
	static constexpr uint32_t kStuckPairedSec = 300;
	std::pair<HS_STATUS, uint32_t> st = homeSpan.getStatus();
	if (st.first == HS_PAIRED && st.second >= kStuckPairedSec) {
		Serial.printf("liveness: HS_PAIRED stuck %us, rebooting to refresh mDNS\n", st.second);
		homeSpan.processSerialCommand("R");
	}
}

void loop() {
	homeSpan.poll();
	checkHomeKitLiveness();
	web::pollConfigApply();  // apply queued live config changes / restart on the HomeSpan task
	web::pollLearnApi();     // drive an in-flight learn capture on the loop task
	pollPendingSends();      // fire scheduled per-command repeats whose delay elapsed
	recovery::poll();
	updateUI();
}
