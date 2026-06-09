#include "HomeSpan.h"

#include "radios.h"
#include "ir.h"
#include "ui.h"
#include "accessory_builder.h"

#include "config_codec.h"
#include "nvs_blob_store.h"

void setup() {
	Serial.begin(115200);

	homeSpan.enableAutoStartAP();
	homeSpan.enableWatchdog(60);
	// OTA intentionally absent: the no-arg default ships a public password; re-add
	// only with a per-device password provisioned out-of-band.
	homeSpan.begin(Category::Bridges, "RF-IR Blaster");

	initRadios();
	initIR();
	initUI();

	config::NvsBlobStore store;
	config::DecodeResult res = config::load(store);
	Serial.printf("config: status=%d usedDefaults=%d devices=%u\n",
	              (int)res.status, res.usedDefaults, (unsigned)res.config.devices.size());
	buildAccessories(res.config);
}

void loop() {
	homeSpan.poll();
	updateUI();
	// TODO: reboot if HomeSpan stays unpaired/disconnected too long (poll getStatus() duration)
}
