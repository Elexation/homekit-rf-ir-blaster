#include "HomeSpan.h"

#include "radios.h"
#include "ir.h"
#include "ui.h"
#include "accessory_builder.h"
#include "recovery.h"
#include "web_server.h"
#include "web_config_api.h"

#include "config_codec.h"
#include "config_model.h"
#include "nvs_blob_store.h"

void setup() {
	Serial.begin(115200);

	homeSpan.setPortNum(config::HAP_PORT);  // move HAP off 80/443 for the config server
	homeSpan.enableAutoStartAP();
	homeSpan.enableWatchdog(60);
	homeSpan.setConnectionCallback(web::start);  // start the config server on WiFi connect (runs once)

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

void loop() {
	homeSpan.poll();
	web::pollConfigApply();  // apply queued live config changes / restart on the HomeSpan task
	recovery::poll();
	updateUI();
}
