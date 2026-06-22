#include "HomeSpan.h"

#include <Arduino.h>

#include "accessory_builder.h"
#include "accessory_plan.h"
#include "radios.h"
#include "ir.h"

#include <map>
#include <string>
#include <vector>

// Single source of truth for live command resolution; build/apply copy into it.
static config::Config g_config;

// aid -> its writable name characteristic, for rename-in-place on a config change.
static std::map<uint16_t, Characteristic::ConfiguredName*> g_names;

static const config::CommandSlot* resolveSlot(uint16_t aid, const char* name) {
	for (const auto& dev : g_config.devices) {
		if (dev.id != aid)
			continue;
		for (const auto& slot : dev.commands)
			if (slot.name == name)
				return &slot;
		break;
	}
	return nullptr;
}

// Fire one resolved code now; false if nothing learned.
static bool fireCode(const config::StoredCode* sc) {
	if (!sc || !sc->isLearned())
		return false;
	if (sc->kind == config::CodeKind::RF) {
		// pulses = one frame + reset gap; replay frameRepeat copies as one keying, clamped to the TX buffer.
		uint8_t reps   = sc->frameRepeat < 1 ? 1 : sc->frameRepeat;
		size_t  unit   = sc->pulses.size();
		size_t  maxRep = unit > 0 ? config::MAX_PULSES / unit : 1;
		if (maxRep < 1)
			maxRep = 1;
		if (reps > maxRep)
			reps = static_cast<uint8_t>(maxRep);
		if (reps <= 1) {
			sendRFCode(sc->freqMHz, sc->asRFCode());
		} else {
			std::vector<uint16_t> burst;
			burst.reserve(reps * unit);
			for (uint8_t r = 0; r < reps; ++r)
				burst.insert(burst.end(), sc->pulses.begin(), sc->pulses.end());
			RFCode code{ burst.data(), static_cast<uint16_t>(burst.size()), sc->freqMHz };
			sendRFCode(sc->freqMHz, code);
		}
	} else if (sc->kind == config::CodeKind::IR) {
		sendIR(sc->asIRCode());
	} else {
		return false;
	}
	return true;
}

// A command's remaining timed sends, scheduled off the loop task. Codes re-resolve
// live at each fire (no dangling pointer). One sequence per accessory; a re-press is ignored.
struct PendingSeq {
	uint16_t    aid;
	std::string name;
	uint8_t     sendsLeft;   // sends still due after the immediate first
	uint16_t    delayMs;
	uint32_t    nextFireMs;
};
static std::vector<PendingSeq> g_pending;

static bool aidBusy(uint16_t aid) {
	for (const auto& p : g_pending)
		if (p.aid == aid)
			return true;
	return false;
}

// Fire the first send now, schedule the rest. False only if nothing is learned (reverts
// the HomeKit tile); a press on a busy accessory is accepted but dropped.
static bool transmit(uint16_t aid, const char* name) {
	const config::CommandSlot* slot = resolveSlot(aid, name);
	if (!slot || !slot->code.isLearned())
		return false;
	if (aidBusy(aid))
		return true;
	fireCode(&slot->code);

	uint8_t count = slot->repeatCount;
	if (count < 1)
		count = 1;
	else if (count > config::MAX_REPEAT_COUNT)
		count = config::MAX_REPEAT_COUNT;
	if (count > 1) {
		uint16_t delay = slot->repeatDelayMs > config::MAX_REPEAT_DELAY_MS
		                     ? config::MAX_REPEAT_DELAY_MS
		                     : slot->repeatDelayMs;
		g_pending.push_back(PendingSeq{
			aid, name, static_cast<uint8_t>(count - 1), delay, millis() + delay });
	}
	return true;
}

static bool hasCommandPrefix(uint16_t aid, const char* prefix) {
	std::string p = prefix;
	for (const auto& dev : g_config.devices) {
		if (dev.id != aid)
			continue;
		for (const auto& slot : dev.commands)
			if (slot.name.rfind(p, 0) == 0)
				return true;
		break;
	}
	return false;
}

// Discrete on/off, falling back to a learned toggle; false if neither is learned.
static bool firePower(uint16_t aid, bool want) {
	return transmit(aid, want ? "on" : "off") || transmit(aid, "toggle");
}

static const char* keyCommand(int remoteKey) {
	switch (remoteKey) {
		case 4:  return "key_up";
		case 5:  return "key_down";
		case 6:  return "key_left";
		case 7:  return "key_right";
		case 8:  return "key_select";
		case 9:  return "key_back";
		case 11: return "key_play_pause";
		case 15: return "key_info";
		default: return "";
	}
}

struct BlasterSwitch : Service::Switch {
	uint16_t aid;
	Characteristic::On power{false};
	Characteristic::ConfiguredName cn;
	BlasterSwitch(uint16_t aid, const char* name) : aid(aid), cn(name) {}
	boolean update() override { return firePower(aid, power.getNewVal<bool>()); }
};

struct BlasterOutlet : Service::Outlet {
	uint16_t aid;
	Characteristic::On power{false};
	Characteristic::OutletInUse inUse{true};
	Characteristic::ConfiguredName cn;
	BlasterOutlet(uint16_t aid, const char* name) : aid(aid), cn(name) {}
	boolean update() override { return firePower(aid, power.getNewVal<bool>()); }
};

struct BlasterLightBulb : Service::LightBulb {
	uint16_t aid;
	Characteristic::On power{false};
	Characteristic::ConfiguredName cn;
	BlasterLightBulb(uint16_t aid, const char* name) : aid(aid), cn(name) {}
	boolean update() override { return firePower(aid, power.getNewVal<bool>()); }
};

struct BlasterFan : Service::Fan {
	uint16_t aid;
	Characteristic::Active power{0};
	Characteristic::ConfiguredName cn;
	BlasterFan(uint16_t aid, const char* name) : aid(aid), cn(name) {}
	boolean update() override { return firePower(aid, power.getNewVal<bool>()); }
};

struct BlasterWindowCovering : Service::WindowCovering {
	uint16_t aid;
	Characteristic::CurrentPosition pos{0};
	Characteristic::TargetPosition target{0};
	Characteristic::ConfiguredName cn;
	BlasterWindowCovering(uint16_t aid, const char* name) : aid(aid), cn(name) {}
	boolean update() override {
		int t = target.getNewVal();
		const char* cmd = t <= 5 ? "down" : (t >= 95 ? "up" : "stop");
		if (!transmit(aid, cmd))
			return false;          // unlearned: Home snaps the slider back
		pos.setVal(t);             // CurrentPosition, not the updated char -> no warning
		return true;
	}
};

// Volume up/down via the Remote widget; stateless, so unlearned just no-ops.
struct BlasterTvSpeaker : Service::TelevisionSpeaker {
	uint16_t aid;
	Characteristic::VolumeControlType volType{1};
	Characteristic::VolumeSelector volSel;
	BlasterTvSpeaker(uint16_t aid) : aid(aid) {}
	boolean update() override {
		if (volSel.updated())
			transmit(aid, volSel.getNewVal() == 0 ? "volume_up" : "volume_down");
		return true;
	}
};

struct BlasterTelevision : Service::Television {
	uint16_t aid;
	Characteristic::Active power{0};
	Characteristic::ActiveIdentifier input{1};
	Characteristic::ConfiguredName cn;
	Characteristic::RemoteKey* remote = nullptr;

	BlasterTelevision(uint16_t aid, const char* name) : aid(aid), cn(name) {
		// RemoteKey must attach to the TV, so create it before any linked service.
		if (hasCommandPrefix(aid, "key_"))
			remote = new Characteristic::RemoteKey();

		SpanService* in = new Service::InputSource();
			new Characteristic::Identifier(1);
			new Characteristic::ConfiguredName(name);
			new Characteristic::IsConfigured(1);
			new Characteristic::CurrentVisibilityState(0);
		addLink(in);

		if (hasCommandPrefix(aid, "volume_"))
			addLink(new BlasterTvSpeaker(aid));
	}

	boolean update() override {
		if (power.updated() && !firePower(aid, power.getNewVal<bool>()))
			return false;
		if (remote && remote->updated())
			transmit(aid, keyCommand(remote->getNewVal()));
		return true;
	}
};

static void createAccessory(const config::PlannedAccessory& acc) {
	new SpanAccessory(acc.aid);
		new Service::AccessoryInformation();
			new Characteristic::Identify();
			new Characteristic::Name(acc.name.c_str());

	const char* name = acc.name.c_str();
	switch (acc.type) {
		case config::ServiceType::Switch:
			g_names[acc.aid] = &(new BlasterSwitch(acc.aid, name))->cn; break;
		case config::ServiceType::Outlet:
			g_names[acc.aid] = &(new BlasterOutlet(acc.aid, name))->cn; break;
		case config::ServiceType::LightBulb:
			g_names[acc.aid] = &(new BlasterLightBulb(acc.aid, name))->cn; break;
		case config::ServiceType::Fan:
			g_names[acc.aid] = &(new BlasterFan(acc.aid, name))->cn; break;
		case config::ServiceType::WindowCovering:
			g_names[acc.aid] = &(new BlasterWindowCovering(acc.aid, name))->cn; break;
		case config::ServiceType::Television:
			g_names[acc.aid] = &(new BlasterTelevision(acc.aid, name))->cn; break;
		case config::ServiceType::Unknown:
			break;  // planner never emits Unknown into accessories
	}
}

void buildAccessories(const config::Config& cfg) {
	g_config = cfg;
	g_names.clear();

	new SpanAccessory(1);
		new Service::AccessoryInformation();
			new Characteristic::Identify();
			new Characteristic::Name("RF-IR Blaster");

	config::PlanResult plan = config::planAccessories(g_config);
	for (const auto& acc : plan.accessories)
		createAccessory(acc);
	for (const auto& sk : plan.skipped)
		Serial.printf("skipped device %u: unknown service \"%s\"\n", sk.id, sk.service.c_str());
}

void applyConfigChange(const config::Config& cfg) {
	auto oldPlan = config::planAccessories(g_config).accessories;
	auto newPlan = config::planAccessories(cfg).accessories;
	config::PlanDiff diff = config::diffPlans(oldPlan, newPlan);

	g_config = cfg;  // swap before creating services so live resolution sees new data
	g_pending.clear();  // abandon in-flight sequences; their codes may be gone now

	for (uint16_t aid : diff.toRemove) {
		homeSpan.deleteAccessory(aid);
		g_names.erase(aid);
	}
	for (const auto& acc : diff.toAdd)
		createAccessory(acc);
	for (const auto& acc : diff.toUpdate) {
		auto it = g_names.find(acc.aid);
		if (it != g_names.end())
			it->second->setString(acc.name.c_str());
	}
	homeSpan.updateDatabase();
}

// Drive scheduled repeats from loop(); re-resolves each code live (no dangling pointer).
void pollPendingSends() {
	if (g_pending.empty())
		return;
	uint32_t now = millis();
	for (size_t i = 0; i < g_pending.size();) {
		PendingSeq& p = g_pending[i];
		if (static_cast<int32_t>(now - p.nextFireMs) < 0) {  // not due yet (wrap-safe)
			++i;
			continue;
		}
		const config::CommandSlot* slot = resolveSlot(p.aid, p.name.c_str());
		bool fired = fireCode(slot ? &slot->code : nullptr);
		if (!fired || --p.sendsLeft == 0) {  // done, or the command vanished
			g_pending.erase(g_pending.begin() + i);
		} else {
			p.nextFireMs = now + p.delayMs;
			++i;
		}
	}
}
