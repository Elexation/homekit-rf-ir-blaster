#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config_model.h"

namespace config {

enum class ServiceType {
	Switch,
	WindowCovering,
	Outlet,
	LightBulb,
	Fan,
	Television,
	Unknown,
};

// Unrecognized names map to Unknown.
ServiceType serviceTypeFromString(const std::string& service);

// code borrows a StoredCode in the source Config; that Config must outlive the plan.
struct PlannedCommand {
	std::string       name;
	const StoredCode* code    = nullptr;
	bool              learned = false;
};

// aid mirrors VirtualDevice.id (the HAP AID).
struct PlannedAccessory {
	uint16_t                    aid  = 0;
	ServiceType                 type = ServiceType::Unknown;
	std::string                 name;
	std::vector<PlannedCommand> commands;
};

// Reported, not silently dropped, so the builder can log the skip.
struct SkippedDevice {
	uint16_t    id = 0;
	std::string service;
};

struct PlanResult {
	std::vector<PlannedAccessory> accessories;
	std::vector<SkippedDevice>    skipped;
};

PlanResult planAccessories(const Config& cfg);

// Keyed by aid. A same-aid type change is emitted as remove+add: a HomeKit
// service class is fixed at construction and can't be morphed in place.
struct PlanDiff {
	std::vector<PlannedAccessory> toAdd;
	std::vector<uint16_t>         toRemove;
	std::vector<PlannedAccessory> toUpdate;
};

PlanDiff diffPlans(const std::vector<PlannedAccessory>& oldPlan,
                   const std::vector<PlannedAccessory>& newPlan);

}  // namespace config
