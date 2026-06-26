#include "ir.h"
#include "rmt_pulse.h"
#include "board_pins.h"

#include <Arduino.h>
#include <esp_err.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_encoder.h>

namespace {

constexpr gpio_num_t kIrTxPin = static_cast<gpio_num_t>(PIN_IR_TX);
// TSMP96000 is open-collector active-low (idle HIGH); needs a pull-up.
constexpr gpio_num_t kIrRxPin = static_cast<gpio_num_t>(PIN_IR_RX);

constexpr uint32_t kResolutionHz = 1000000;  // 1 us per tick
constexpr size_t   kMemBlocks    = 48;        // non-DMA: multiple of 48 on the S3
constexpr uint16_t kDefaultCarrierHz = 38000;
constexpr uint16_t kCarrierMinHz = 20000;
constexpr uint16_t kCarrierMaxHz = 60000;
constexpr size_t   kMaxSymbols   = config::MAX_PULSES / 2;

constexpr size_t   kRxMemBlocks  = 48;
constexpr uint32_t kRxGlitchNs   = 2000;        // source-clocked filter, max 255 ticks
constexpr uint32_t kRxIdleNs     = 12000000;    // 12 ms
// Carrier-preserved capture is symbol-dense; too small truncates the frame tail (no partial-rx).
constexpr size_t   kRxBufSymbols = 4096;
constexpr uint32_t kEnvGapUs     = 150;         // idle-high >= this is an envelope space

rmt_symbol_word_t    s_symbols[kMaxSymbols];  // TX scratch (channel built on demand)

rmt_channel_handle_t s_rx           = nullptr;
bool                 s_rxEnabled    = false;
rmt_symbol_word_t    s_rxBuf[kRxBufSymbols];
volatile bool        s_frameReady   = false;
volatile size_t      s_frameSymbols = 0;

rmt_receive_config_t makeRxConfig() {
	rmt_receive_config_t c = {};
	c.signal_range_min_ns = kRxGlitchNs;
	c.signal_range_max_ns = kRxIdleNs;
	c.flags.en_partial_rx = 0;
	return c;
}

// ISR context: record the frame; collapse runs on the loop task.
bool IRAM_ATTR rxDoneCb(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata, void*) {
	s_frameSymbols = edata->num_symbols;
	s_frameReady = true;
	return false;
}

}  // namespace

void initIR() {
	rmt_rx_channel_config_t rxcfg = {};
	rxcfg.gpio_num = kIrRxPin;
	rxcfg.clk_src = RMT_CLK_SRC_DEFAULT;
	rxcfg.resolution_hz = kResolutionHz;
	rxcfg.mem_block_symbols = kRxMemBlocks;
	rxcfg.intr_priority = 3;  // must match the Pixel RMT group
	esp_err_t err = rmt_new_rx_channel(&rxcfg, &s_rx);
	if (err != ESP_OK) {
		Serial.printf("[ir] rmt_new_rx_channel failed: %s\n", esp_err_to_name(err));
		return;
	}
	rmt_rx_event_callbacks_t cbs = {};
	cbs.on_recv_done = rxDoneCb;
	err = rmt_rx_register_event_callbacks(s_rx, &cbs, nullptr);
	if (err != ESP_OK) {
		Serial.printf("[ir] rmt_rx_register_event_callbacks failed: %s\n", esp_err_to_name(err));
		return;
	}
	Serial.println("[ir] RMT RX ready on GP2");
}

bool sendIR(const IRCode& code) {
	if (!code.pulses || code.length < 2 || (code.length & 1))
		return false;
	if (s_rxEnabled) {  // a learn holds the IR RX; the TSMP would hear our own LED
		Serial.println("[ir] sendIR: IR RX active, TX skipped");
		return false;
	}

	uint16_t carrier = code.carrierHz;
	if (carrier < kCarrierMinHz || carrier > kCarrierMaxHz)
		carrier = kDefaultCarrierHz;

	// TX channel built on demand and freed after, so a learn can hold the RMT slot.
	rmt_tx_channel_config_t cfg = {};
	cfg.gpio_num = kIrTxPin;
	cfg.clk_src = RMT_CLK_SRC_DEFAULT;
	cfg.resolution_hz = kResolutionHz;
	cfg.mem_block_symbols = kMemBlocks;
	cfg.trans_queue_depth = 4;
	cfg.intr_priority = 3;
	rmt_channel_handle_t tx = nullptr;
	esp_err_t e = rmt_new_tx_channel(&cfg, &tx);
	if (e != ESP_OK) {
		Serial.printf("[ir] rmt_new_tx_channel failed: %s\n", esp_err_to_name(e));
		return false;
	}
	rmt_encoder_handle_t enc = nullptr;
	rmt_copy_encoder_config_t ec = {};
	if (rmt_new_copy_encoder(&ec, &enc) != ESP_OK) {
		rmt_del_channel(tx);
		return false;
	}
	rmt_carrier_config_t carrierCfg = {};
	carrierCfg.frequency_hz = carrier;
	carrierCfg.duty_cycle = 0.33f;
	carrierCfg.flags.polarity_active_low = 0;  // carrier rides the mark level
	e = rmt_apply_carrier(tx, &carrierCfg);
	if (e != ESP_OK) {  // no carrier -> receiver can't demodulate; fail the send so the tile reverts
		Serial.printf("[ir] rmt_apply_carrier(%u) failed: %s\n", carrier, esp_err_to_name(e));
		rmt_del_encoder(enc);
		rmt_del_channel(tx);
		return false;
	}
	if (rmt_enable(tx) != ESP_OK) {
		rmt_del_encoder(enc);
		rmt_del_channel(tx);
		return false;
	}

	size_t symbols = packPulsesToSymbols(code.pulses, code.length, s_symbols, kMaxSymbols);
	bool sent = false;
	if (symbols > 0) {
		rmt_transmit_config_t tc = {};
		tc.loop_count = 0;
		tc.flags.eot_level = 0;  // idle low after the frame -> LED off
		if (rmt_transmit(tx, enc, s_symbols, symbols * sizeof(rmt_symbol_word_t), &tc) == ESP_OK) {
			rmt_tx_wait_all_done(tx, 1000);
			sent = true;
		}
	}

	rmt_disable(tx);
	rmt_del_encoder(enc);
	rmt_del_channel(tx);
	return sent;
}

void startIRRx() {
	if (!s_rx)
		return;
	if (!s_rxEnabled) {
		esp_err_t e = rmt_enable(s_rx);
		if (e != ESP_OK) {
			Serial.printf("[ir] rmt_enable(rx) failed: %s\n", esp_err_to_name(e));
			return;
		}
		s_rxEnabled = true;
	}
	s_frameReady = false;
	rmt_receive_config_t c = makeRxConfig();
	esp_err_t e = rmt_receive(s_rx, s_rxBuf, sizeof(s_rxBuf), &c);
	if (e != ESP_OK)
		Serial.printf("[ir] rmt_receive(rx) failed: %s\n", esp_err_to_name(e));
}

void stopIRRx() {
	if (!s_rx || !s_rxEnabled)
		return;
	rmt_disable(s_rx);
	s_rxEnabled = false;
	s_frameReady = false;
}

// Collapse one carrier-preserved frame into a us mark/space envelope + carrier (TSMP idle HIGH).
bool pollIRBurst(uint16_t* out, size_t cap, size_t* outLen, uint16_t* outCarrierHz) {
	if (!s_rx || !s_frameReady)
		return false;

	size_t   n            = s_frameSymbols;
	size_t   outN         = 0;
	uint32_t markAccum    = 0;
	bool     inMark       = false;
	bool     lastWasSpace = false;
	uint64_t activeHalves = 0;  // one carrier-active (low) half per cycle
	uint64_t totalMarkUs  = 0;

	auto push = [&](uint32_t us, bool space) {
		if (outN < cap) {
			out[outN++] = us > 65535 ? 65535 : static_cast<uint16_t>(us);
			lastWasSpace = space;
		}
	};

	for (size_t i = 0; i < n; ++i) {
		uint32_t durs[2] = { s_rxBuf[i].duration0, s_rxBuf[i].duration1 };
		uint8_t  lvls[2] = { static_cast<uint8_t>(s_rxBuf[i].level0),
		                     static_cast<uint8_t>(s_rxBuf[i].level1) };
		for (int h = 0; h < 2; ++h) {
			uint32_t dur = durs[h];
			if (dur == 0)
				continue;  // end-of-frame filler
			if (lvls[h] == 1 && dur >= kEnvGapUs) {  // idle held long -> envelope space
				if (inMark) {
					push(markAccum, false);
					markAccum = 0;
					inMark = false;
				}
				push(dur, true);
			} else {  // carrier toggling -> mark
				markAccum += dur;
				totalMarkUs += dur;
				inMark = true;
				if (lvls[h] == 0)
					++activeHalves;
			}
		}
	}
	if (inMark)
		push(markAccum, false);
	if (lastWasSpace && outN > 0)
		--outN;  // drop the inter-frame gap so the burst ends on a mark

	s_frameReady = false;
	rmt_receive_config_t c = makeRxConfig();
	esp_err_t re = rmt_receive(s_rx, s_rxBuf, sizeof(s_rxBuf), &c);
	if (re != ESP_OK)
		Serial.printf("[ir] rmt_receive re-arm failed: %s\n", esp_err_to_name(re));

	if (outN == 0)
		return false;
	*outLen = outN;
	uint64_t hz = totalMarkUs ? activeHalves * 1000000ull / totalMarkUs : 0;
	*outCarrierHz = hz > 65535 ? 65535 : static_cast<uint16_t>(hz);
	return true;
}
