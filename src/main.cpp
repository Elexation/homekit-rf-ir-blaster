#include "HomeSpan.h"

#include "radios.h"
#include "ir.h"
#include "ui.h"
#include "accessory_builder.h"
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
	web::begin(res.config.settings);  // stash settings before the WiFi callback can fire
	web::configApiBegin(res.config);  // seed the config API (own NVS store + rev) before the server starts
	buildAccessories(res.config);
}

void loop() {
	homeSpan.poll();
	web::pollConfigApply();  // apply queued live config changes / restart on the HomeSpan task
	updateUI();
}
