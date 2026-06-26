#include "learn_machine.h"

#include <algorithm>
#include <vector>

#include "frame_merge.h"

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

	// RF: de-clip repeated frames before matching/storing to undo gain clipping; IR is clean.
	const uint16_t* fp = pulses;
	size_t          fl = len;
	std::vector<uint16_t> declipped;
	if (src != Source::Ir) {
		declipped.resize(len);
		uint16_t gap = 0, frames = 0;
		size_t dl = declipBurst(pulses, len, declipped.data(), declipped.size(), &gap, &frames);
		if (dl > 0) {
			fp = declipped.data();
			fl = dl;
			if (gap > 0)
				t.resetGapUs = std::max(t.resetGapUs, gap);  // keep the longest gap across presses
			if (frames > 1 && frames > t.frameCount)
				t.frameCount = frames;
		}
	}

	int cl = -1;
	if (wellFormed(src, fp, fl, carrierHz)) {
		for (size_t i = 0; i < t.clusters.size(); ++i) {
			if (burstsMatch(t.clusters[i].repr.data(), t.clusters[i].repr.size(), fp, fl)) {
				cl = static_cast<int>(i);
				break;
			}
		}
		if (cl < 0) {
			int slot = -1;
			if (t.clusters.size() < kMaxClusters) {
				t.clusters.push_back(Cluster{});
				slot = static_cast<int>(t.clusters.size() - 1);
			} else {
				// Slots full: evict the weakest cluster so a real repeat still fits
				// (ambient noise forms low-count clusters). Protect an established
				// candidate or anything seen across >= kRfPressesNeeded presses.
				int weakest = -1;
				for (size_t i = 0; i < t.clusters.size(); ++i) {
					if (t.clusters[i].count >= kMatchesNeeded ||
					    t.clusters[i].groupCount >= kRfPressesNeeded)
						continue;
					if (weakest < 0 ||
					    t.clusters[i].count < t.clusters[static_cast<size_t>(weakest)].count)
						weakest = static_cast<int>(i);
				}
				if (weakest >= 0) {
					for (Group& g : t.groups)
						if (g.firstCluster == weakest) {
							g.firstCluster = -1;
							g.dirty = true;
						}
					t.clusters[static_cast<size_t>(weakest)] = Cluster{};
					slot = weakest;
				}
			}
			if (slot >= 0) {
				Cluster& c = t.clusters[static_cast<size_t>(slot)];
				c.repr.assign(fp, fp + fl);
				c.sum.assign(fl, 0);
				cl = slot;
			}
		}
		if (cl >= 0) {
			Cluster& c = t.clusters[static_cast<size_t>(cl)];
			if (c.count < kMaxClusterMembers) {
				++c.count;
				for (size_t i = 0; i < fl; ++i)
					c.sum[i] += fp[i];
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

	// Per-entry average of the matched frames (de-clipped for RF).
	size_t len = c.repr.size();
	std::vector<uint16_t> frame(len);
	for (size_t i = 0; i < len; ++i)
		frame[i] = static_cast<uint16_t>((c.sum[i] + c.count / 2) / c.count);

	uint16_t resetGap = tracks_[static_cast<size_t>(src)].resetGapUs;
	if (src != Source::Ir) {
		// Store one frame + its reset gap; frameRepeat replays it at transmit. Gap:
		// the de-clip's in-burst value, else the measured inter-capture gap, else default.
		uint32_t gap = resetGap > 0
		                   ? resetGap
		                   : (c.gapCount > 0 ? (c.gapSumUs + c.gapCount / 2) / c.gapCount
		                                     : kDefaultTrailGapUs);
		gap = std::min<uint32_t>(std::max<uint32_t>(gap, kMinTrailGapUs), 65535);
		code.pulses = std::move(frame);                    // ends on a mark (odd)
		code.pulses.push_back(static_cast<uint16_t>(gap));  // + gap -> even
		uint16_t count = tracks_[static_cast<size_t>(src)].frameCount;
		if (count < 1)
			count = static_cast<uint16_t>(kStoredFrameCount);  // single-frame capture fallback
		code.frameRepeat = static_cast<uint8_t>(std::min<uint16_t>(count, config::MAX_FRAME_REPEAT));
	} else {
		code.pulses = std::move(frame);
		if (len % 2 != 0) {
			uint32_t gap = c.gapCount > 0 ? (c.gapSumUs + c.gapCount / 2) / c.gapCount
			                              : kDefaultTrailGapUs;
			gap = std::max<uint32_t>(gap, kMinTrailGapUs);
			code.pulses.push_back(static_cast<uint16_t>(std::min<uint32_t>(gap, 65535)));
		}
	}

	code_ = std::move(code);
	for (auto& t : tracks_)
		t = Track{};
	state_ = State::Captured;
}

}  // namespace learn
