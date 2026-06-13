#include "learn_machine.h"

#include <algorithm>

namespace learn {

namespace {

uint32_t sumUs(const uint16_t* pulses, size_t len) {
	uint32_t total = 0;
	for (size_t i = 0; i < len; ++i)
		total += pulses[i];
	return total;
}

}  // namespace

void LearnMachine::begin(uint64_t now) {
	reset();
	state_ = State::Listening;
	deadline_ = now + kWindowMs;
}

void LearnMachine::cancel() {
	reset();
}

void LearnMachine::reset() {
	for (auto& t : tracks_)
		t = Track{};
	code_ = config::StoredCode{};
	deadline_ = 0;
	state_ = State::Idle;
	reason_ = FailReason::Timeout;
	sawActivity_ = false;
}

State LearnMachine::tick(uint64_t now) {
	if (state_ == State::Listening && now >= deadline_)
		fail(sawActivity_ ? FailReason::Noisy : FailReason::Timeout);
	return state_;
}

void LearnMachine::fail(FailReason reason) {
	for (auto& t : tracks_)
		t = Track{};
	reason_ = reason;
	state_ = State::Failed;
}

config::StoredCode LearnMachine::takeCode() {
	if (state_ != State::Captured)
		return config::StoredCode{};
	config::StoredCode out = std::move(code_);
	reset();
	return out;
}

void LearnMachine::feedBurst(Source src, const uint16_t* pulses, size_t len,
                             uint16_t carrierHz, uint64_t now) {
	if (state_ != State::Listening || pulses == nullptr || len == 0)
		return;
	sawActivity_ = true;
	Track& t = tracks_[static_cast<size_t>(src)];

	uint64_t durMs = sumUs(pulses, len) / 1000;
	uint64_t start = now > durMs ? now - durMs : 0;
	bool newGroup = !t.sawBurst || start > t.lastEnd + kPressGapMs;

	if (newGroup) {
		++t.curGroup;
		if (t.groups.size() < kMaxGroups)
			t.groups.push_back(Group{});
	} else if (t.lastCluster >= 0 && start > t.lastEnd) {
		// The silence before this burst is the previous burst's trailing gap.
		Cluster& prev = t.clusters[static_cast<size_t>(t.lastCluster)];
		if (prev.gapCount < kMaxGapSamples) {
			uint64_t gapUs = (start - t.lastEnd) * 1000;
			prev.gapSumUs += static_cast<uint32_t>(std::min<uint64_t>(gapUs, 65535));
			++prev.gapCount;
		}
	}

	int cl = -1;
	if (wellFormed(src, pulses, len, carrierHz)) {
		for (size_t i = 0; i < t.clusters.size(); ++i) {
			if (burstsMatch(t.clusters[i].repr.data(), t.clusters[i].repr.size(), pulses, len)) {
				cl = static_cast<int>(i);
				break;
			}
		}
		if (cl < 0 && t.clusters.size() < kMaxClusters) {
			Cluster c;
			c.repr.assign(pulses, pulses + len);
			c.sum.assign(len, 0);
			t.clusters.push_back(std::move(c));
			cl = static_cast<int>(t.clusters.size() - 1);
		}
		if (cl >= 0) {
			Cluster& c = t.clusters[static_cast<size_t>(cl)];
			if (c.count < kMaxClusterMembers) {
				++c.count;
				for (size_t i = 0; i < len; ++i)
					c.sum[i] += pulses[i];
				c.carrierSum += carrierHz;
			}
			if (c.lastGroup != t.curGroup) {
				c.lastGroup = t.curGroup;
				++c.groupCount;
			}
		}
		if (t.curGroup >= 1 && t.curGroup <= t.groups.size()) {
			Group& g = t.groups[t.curGroup - 1];
			if (g.firstCluster < 0 && cl >= 0)
				g.firstCluster = static_cast<int8_t>(cl);
			else if (cl < 0 || cl != g.firstCluster)
				g.dirty = true;
		}
	}

	t.lastEnd = now;
	t.lastCluster = cl;
	t.sawBurst = true;

	if (cl >= 0) {
		const Cluster& c = t.clusters[static_cast<size_t>(cl)];
		bool enoughPresses = src == Source::Ir || c.groupCount >= kRfPressesNeeded;
		if (c.count >= kMatchesNeeded && enoughPresses) {
			capture(src, c);
			return;
		}
	}
	if (src != Source::Ir)
		checkRolling(t);
}

void LearnMachine::checkRolling(Track& t) {
	// A payload repeated across presses proves a fixed code, never rolling.
	for (const Cluster& c : t.clusters) {
		if (c.groupCount >= kRfPressesNeeded)
			return;
	}

	// Distinct payloads, each the sole well-formed content of its own press.
	int8_t ids[kMaxGroups];
	size_t n = 0;
	for (const Group& g : t.groups) {
		if (g.firstCluster < 0 || g.dirty)
			continue;
		bool seen = false;
		for (size_t i = 0; i < n; ++i) {
			if (ids[i] == g.firstCluster) {
				seen = true;
				break;
			}
		}
		if (!seen)
			ids[n++] = g.firstCluster;
	}
	if (n < kRollingPressesNeeded)
		return;

	// Rolling only when enough payloads share one frame structure; noise doesn't.
	for (size_t a = 0; a < n; ++a) {
		const Cluster& ca = t.clusters[static_cast<size_t>(ids[a])];
		size_t alike = 0;
		for (size_t b = 0; b < n; ++b) {
			const Cluster& cb = t.clusters[static_cast<size_t>(ids[b])];
			if (structurallyAlike(ca.repr.data(), ca.repr.size(),
			                      cb.repr.data(), cb.repr.size()))
				++alike;
		}
		if (alike >= kRollingPressesNeeded) {
			fail(FailReason::Rolling);
			return;
		}
	}
}

void LearnMachine::capture(Source src, const Cluster& c) {
	config::StoredCode code;
	if (src == Source::Ir) {
		code.kind = config::CodeKind::IR;
		code.carrierHz = static_cast<uint16_t>((c.carrierSum + c.count / 2) / c.count);
	} else {
		code.kind = config::CodeKind::RF;
		code.freqMHz = src == Source::Rf315 ? 315 : 433;
	}

	size_t len = c.repr.size();
	code.pulses.resize(len);
	for (size_t i = 0; i < len; ++i)
		code.pulses[i] = static_cast<uint16_t>((c.sum[i] + c.count / 2) / c.count);
	if (len % 2 != 0) {
		uint32_t gap = c.gapCount > 0 ? (c.gapSumUs + c.gapCount / 2) / c.gapCount
		                              : kDefaultTrailGapUs;
		gap = std::max<uint32_t>(gap, kMinTrailGapUs);
		code.pulses.push_back(static_cast<uint16_t>(std::min<uint32_t>(gap, 65535)));
	}

	code_ = std::move(code);
	for (auto& t : tracks_)
		t = Track{};
	state_ = State::Captured;
}

}  // namespace learn
