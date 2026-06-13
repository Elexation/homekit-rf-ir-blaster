#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "config_model.h"
#include "signal_match.h"

namespace learn {

enum class State : uint8_t { Idle, Listening, Captured, Failed };

// Timeout = no activity; Noisy = activity but no matching repeats; Rolling = consistent presses, differing payload.
enum class FailReason : uint8_t { Timeout, Noisy, Rolling };

class LearnMachine {
public:
	static constexpr uint32_t kWindowMs = 30000;
	static constexpr uint32_t kPressGapMs = 250;
	static constexpr uint8_t kMatchesNeeded = 3;
	// RF needs matches across two presses: a held rolling fob repeats one spent word. IR exempt.
	static constexpr uint8_t kRfPressesNeeded = 2;
	// Three (not two) so alternating two buttons of a fixed remote isn't rolling.
	static constexpr uint8_t kRollingPressesNeeded = 3;
	// Trailing gap stored when no inter-repeat gap was observed.
	static constexpr uint16_t kDefaultTrailGapUs = 30000;
	static constexpr uint16_t kMinTrailGapUs = 5000;
	static constexpr size_t kMaxClusters = 6;   // pulse trains tracked per source
	static constexpr size_t kMaxGroups   = 16;  // press groups tracked per source

	void begin(uint64_t now);  // arms a fresh session
	void cancel();             // back to Idle, dropping any code

	// One completed burst; microsecond mark/space `pulses`, `now` = final-edge time, carrierHz 0 for RF; ignored unless Listening.
	void feedBurst(Source src, const uint16_t* pulses, size_t len,
	               uint16_t carrierHz, uint64_t now);

	State tick(uint64_t now);  // applies the window deadline; safe every poll

	State      state() const { return state_; }
	FailReason failReason() const { return reason_; }
	bool       active() const { return state_ == State::Listening; }

	config::StoredCode takeCode();  // Captured to Idle handover; else unlearned

private:
	struct Cluster {
		std::vector<uint16_t> repr;  // first burst; the match reference
		std::vector<uint32_t> sum;   // per-entry totals across members, for averaging
		uint32_t carrierSum = 0;
		uint32_t gapSumUs   = 0;
		uint16_t lastGroup  = 0;
		uint8_t  gapCount   = 0;
		uint8_t  count      = 0;
		uint8_t  groupCount = 0;  // distinct presses this payload appeared in
	};
	struct Group {
		int8_t firstCluster = -1;  // cluster of the press's first well-formed burst
		bool   dirty        = false;  // press held anything outside that cluster
	};
	struct Track {
		std::vector<Cluster> clusters;
		std::vector<Group>   groups;
		uint64_t lastEnd     = 0;
		uint16_t curGroup    = 0;
		int      lastCluster = -1;
		bool     sawBurst    = false;
	};

	static constexpr uint8_t kMaxClusterMembers = 250;
	static constexpr uint8_t kMaxGapSamples     = 250;

	void reset();
	void fail(FailReason reason);
	void capture(Source src, const Cluster& c);
	void checkRolling(Track& t);

	Track              tracks_[3];
	config::StoredCode code_;
	uint64_t           deadline_    = 0;
	State              state_       = State::Idle;
	FailReason         reason_      = FailReason::Timeout;
	bool               sawActivity_ = false;
};

}  // namespace learn
