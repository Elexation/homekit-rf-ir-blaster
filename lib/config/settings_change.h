#pragma once

#include "config_model.h"

namespace config {

// Network bindings are read at startup, so changing one needs a restart; ledEnabled applies live.
enum class ApplyKind { Live, RequiresRestart };

inline ApplyKind classifyChange(const Settings& oldS, const Settings& newS) {
	if (oldS.https               != newS.https ||
	    oldS.listenPort          != newS.listenPort ||
	    oldS.trustedProxy        != newS.trustedProxy ||
	    oldS.canonicalDomain     != newS.canonicalDomain)
		return ApplyKind::RequiresRestart;
	return ApplyKind::Live;
}

inline bool requiresRestart(const Settings& oldS, const Settings& newS) {
	return classifyChange(oldS, newS) == ApplyKind::RequiresRestart;
}

}  // namespace config
