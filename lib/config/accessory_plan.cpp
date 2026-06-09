#include "accessory_plan.h"

#include <map>

namespace config {

ServiceType serviceTypeFromString(const std::string& service) {
	if (service == "Switch")         return ServiceType::Switch;
	if (service == "WindowCovering") return ServiceType::WindowCovering;
	if (service == "Outlet")         return ServiceType::Outlet;
	if (service == "LightBulb")      return ServiceType::LightBulb;
	if (service == "Fan")            return ServiceType::Fan;
	if (service == "Television")     return ServiceType::Television;
	return ServiceType::Unknown;
}

PlanResult planAccessories(const Config& cfg) {
	PlanResult result;
	for (const auto& device : cfg.devices) {
		ServiceType type = serviceTypeFromString(device.service);
		if (type == ServiceType::Unknown) {
			result.skipped.push_back({ device.id, device.service });
			continue;
		}
		PlannedAccessory acc;
		acc.aid  = device.id;
		acc.type = type;
		acc.name = device.name;
		for (const auto& slot : device.commands) {
			PlannedCommand cmd;
			cmd.name    = slot.name;
			cmd.code    = &slot.code;
			cmd.learned = slot.code.isLearned();
			acc.commands.push_back(std::move(cmd));
		}
		result.accessories.push_back(std::move(acc));
	}
	return result;
}

// Compare by name+learned, not code pointers: those address different Configs.
static bool commandsEqual(const std::vector<PlannedCommand>& a,
                          const std::vector<PlannedCommand>& b) {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].name != b[i].name || a[i].learned != b[i].learned)
			return false;
	}
	return true;
}

PlanDiff diffPlans(const std::vector<PlannedAccessory>& oldPlan,
                   const std::vector<PlannedAccessory>& newPlan) {
	PlanDiff diff;

	std::map<uint16_t, const PlannedAccessory*> oldByAid;
	for (const auto& a : oldPlan)
		oldByAid[a.aid] = &a;
	std::map<uint16_t, const PlannedAccessory*> newByAid;
	for (const auto& a : newPlan)
		newByAid[a.aid] = &a;

	for (const auto& n : newPlan) {
		auto it = oldByAid.find(n.aid);
		if (it == oldByAid.end()) {
			diff.toAdd.push_back(n);
			continue;
		}
		const PlannedAccessory& o = *it->second;
		if (o.type != n.type) {
			diff.toRemove.push_back(o.aid);
			diff.toAdd.push_back(n);
		} else if (o.name != n.name || !commandsEqual(o.commands, n.commands)) {
			diff.toUpdate.push_back(n);
		}
	}

	for (const auto& o : oldPlan) {
		if (newByAid.find(o.aid) == newByAid.end())
			diff.toRemove.push_back(o.aid);
	}

	return diff;
}

}  // namespace config
