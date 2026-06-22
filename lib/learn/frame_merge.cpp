#include "frame_merge.h"

#include <algorithm>
#include <vector>

namespace learn {

namespace {

struct Range {
	size_t begin;
	size_t end;  // exclusive; frame body ends on a mark, before the reset gap
};

}  // namespace

size_t declipBurst(const uint16_t* in, size_t len,
                   uint16_t* out, size_t outCap, uint16_t* resetGapUs,
                   uint16_t* outFrames) {
	*resetGapUs = 0;
	if (outFrames)
		*outFrames = 0;
	if (in == nullptr || out == nullptr || outCap == 0 || len == 0)
		return 0;

	// Split on reset gaps; a frame body excludes its trailing gap.
	std::vector<Range> frames;
	std::vector<uint16_t> gaps;
	size_t frameStart = 0;
	for (size_t i = 0; i < len; ++i) {
		bool isSpace = (i % 2 == 1);
		if (isSpace && in[i] >= kResetGapMinUs) {
			if (i > frameStart)
				frames.push_back({ frameStart, i });
			gaps.push_back(in[i]);
			frameStart = i + 1;
		}
	}
	if (frameStart < len)
		frames.push_back({ frameStart, len });  // trailing frame; may be truncated
	if (frames.empty())
		return 0;

	// Single frame: pass through unchanged.
	if (frames.size() == 1) {
		size_t n = std::min(frames[0].end - frames[0].begin, outCap);
		std::copy(in + frames[0].begin, in + frames[0].begin + n, out);
		if (outFrames)
			*outFrames = 1;
		return n;
	}

	// Merge only modal-length frames; drop a partial first or truncated last.
	size_t modal = 0, modalCount = 0;
	for (const Range& a : frames) {
		size_t aLen = a.end - a.begin;
		size_t cnt = 0;
		for (const Range& b : frames)
			if (b.end - b.begin == aLen)
				++cnt;
		if (cnt > modalCount || (cnt == modalCount && aLen > modal)) {
			modalCount = cnt;
			modal = aLen;
		}
	}

	size_t n = std::min(modal, outCap);
	if (n == 0)
		return 0;
	bool first = true;
	for (const Range& f : frames) {
		if (f.end - f.begin != modal)
			continue;
		for (size_t i = 0; i < n; ++i) {
			uint16_t v = in[f.begin + i];
			bool isMark = (i % 2 == 0);
			if (first)
				out[i] = v;
			else if (isMark)
				out[i] = std::max(out[i], v);
			else
				out[i] = std::min(out[i], v);
		}
		first = false;
	}
	if (first)
		return 0;

	// True reset gap = the longest inter-frame pause; noise only chops gaps shorter, never longer.
	*resetGapUs = *std::max_element(gaps.begin(), gaps.end());
	if (outFrames)
		*outFrames = static_cast<uint16_t>(modalCount);
	return n;
}

}  // namespace learn
