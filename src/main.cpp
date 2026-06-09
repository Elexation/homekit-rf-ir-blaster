#include "HomeSpan.h"

#include "radios.h"
#include "ir.h"
#include "codes.h"
#include "ui.h"

struct ProjectorScreen : Service::WindowCovering {
	Characteristic::CurrentPosition pos{0};
	Characteristic::TargetPosition target{0};

	boolean update() override {
		int t = target.getNewVal();
		if      (t <= 5)  sendRFCode(315, SCREEN_DOWN);
		else if (t >= 95) sendRFCode(315, SCREEN_UP);
		else              sendRFCode(315, SCREEN_STOP);
		pos.setVal(t);
		return true;
	}
};

struct ProjectorPower : Service::Switch {
	Characteristic::On power{0};

	boolean update() override {
		sendIR(POWER_TOGGLE);
		return true;
	}
};

void setup() {
	Serial.begin(115200);

	homeSpan.enableAutoStartAP();
	homeSpan.enableWatchdog(60);
	homeSpan.enableOTA();
	homeSpan.begin(Category::Bridges, "RF-IR Blaster");

	initRadios();
	initIR();
	initUI();
	loadCodes();

	new SpanAccessory();
		new Service::AccessoryInformation();
			new Characteristic::Identify();
			new Characteristic::Name("RF-IR Blaster");

	new SpanAccessory();
		new Service::AccessoryInformation();
			new Characteristic::Identify();
			new Characteristic::Name("Projector Screen");
		new ProjectorScreen();

	new SpanAccessory();
		new Service::AccessoryInformation();
			new Characteristic::Identify();
			new Characteristic::Name("Projector");
		new ProjectorPower();
}

void loop() {
	homeSpan.poll();
	updateUI();
	// TODO: HomeKit liveness / zombie-reboot check once HS_STATUS enum names are verified
}
