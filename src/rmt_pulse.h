#pragma once

#include <cstddef>
#include <cstdint>
#include <driver/rmt_types.h>

// Pack mark/space us pairs into RMT symbols (mark = level 1, space = level 0), splitting any
// duration past the 15-bit field (32767). Carrier applied separately; returns the symbol count.
inline size_t packPulsesToSymbols(const uint16_t* pulses, size_t length,
                                  rmt_symbol_word_t* out, size_t maxSymbols) {
	constexpr uint32_t kRmtMaxDur = 32767;  // RMT duration is a 15-bit field
	const size_t maxHalves = maxSymbols * 2;
	size_t h = 0;  // half-cell index; two halves per symbol word
	auto pushHalf = [&](uint8_t level, uint32_t dur) {
		while (dur > 0 && h < maxHalves) {
			uint32_t chunk = dur > kRmtMaxDur ? kRmtMaxDur : dur;
			rmt_symbol_word_t& w = out[h / 2];
			if (h & 1) {
				w.level1 = level;
				w.duration1 = chunk;
			} else {
				w.level0 = level;
				w.duration0 = chunk;
			}
			++h;
			dur -= chunk;
		}
	};
	size_t pairs = length / 2;
	for (size_t i = 0; i < pairs && h < maxHalves; ++i) {
		pushHalf(1, pulses[2 * i]);      // mark: carrier / PA on
		pushHalf(0, pulses[2 * i + 1]);  // space: off
	}
	if (h == 0)
		return 0;
	if (h & 1) {  // zero the unused trailing half so the frame terminates cleanly
		out[h / 2].level1 = 0;
		out[h / 2].duration1 = 0;
	}
	return (h + 1) / 2;
}
