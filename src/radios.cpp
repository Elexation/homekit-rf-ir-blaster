#include "radios.h"
#include "rmt_pulse.h"
#include "board_pins.h"

#include <Arduino.h>
#include <SPI.h>
#include <esp_err.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

namespace {

constexpr int8_t kSck315   = PIN_RF315_SCK;
constexpr int8_t kMiso315  = PIN_RF315_MISO;
constexpr int8_t kMosi315  = PIN_RF315_MOSI;
constexpr int8_t kCs315    = PIN_RF315_CS;
constexpr int8_t kGdo0_315 = PIN_RF315_GDO0;
constexpr int8_t kSck433   = PIN_RF433_SCK;
constexpr int8_t kMiso433  = PIN_RF433_MISO;
constexpr int8_t kMosi433  = PIN_RF433_MOSI;
constexpr int8_t kCs433    = PIN_RF433_CS;
constexpr int8_t kGdo0_433 = PIN_RF433_GDO0;

constexpr uint8_t kSres       = 0x30;  // reset strobe
constexpr uint8_t kSrx        = 0x34;  // enter RX
constexpr uint8_t kStx        = 0x35;  // enter TX
constexpr uint8_t kSidle      = 0x36;  // IDLE
constexpr uint8_t kWriteBurst = 0x40;  // burst bit
constexpr uint8_t kRegPatable = 0x3E;  // PA table

const SPISettings kSpiSettings(1000000, MSBFIRST, SPI_MODE0);

SPIClass spi315(FSPI);
SPIClass spi433(HSPI);

void waitMisoLow(int8_t misoPin) {
	// SO low = chip ready; bounded so a miswired board can't hang.
	uint32_t t0 = millis();
	while (digitalRead(misoPin) && (millis() - t0) < 10) {
	}
}

void bringUpBus(SPIClass& spi, int8_t sck, int8_t miso, int8_t mosi, int8_t cs) {
	pinMode(cs, OUTPUT);
	digitalWrite(cs, HIGH);
	spi.begin(sck, miso, mosi, -1);  // CS driven manually
}

void resetRadio(SPIClass& spi, int8_t csPin, int8_t misoPin) {
	// CC1101 manual power-on reset sequence.
	digitalWrite(csPin, LOW);
	delayMicroseconds(10);
	digitalWrite(csPin, HIGH);
	delayMicroseconds(45);
	digitalWrite(csPin, LOW);
	waitMisoLow(misoPin);
	spi.beginTransaction(kSpiSettings);
	spi.transfer(kSres);
	spi.endTransaction();
	waitMisoLow(misoPin);
	digitalWrite(csPin, HIGH);
}

void writeReg(SPIClass& spi, int8_t csPin, uint8_t addr, uint8_t val) {
	digitalWrite(csPin, LOW);
	spi.beginTransaction(kSpiSettings);
	spi.transfer(addr);
	spi.transfer(val);
	spi.endTransaction();
	digitalWrite(csPin, HIGH);
}

void strobe(SPIClass& spi, int8_t csPin, uint8_t cmd) {
	digitalWrite(csPin, LOW);
	spi.beginTransaction(kSpiSettings);
	spi.transfer(cmd);
	spi.endTransaction();
	digitalWrite(csPin, HIGH);
}

void writeBurst(SPIClass& spi, int8_t csPin, uint8_t addr, const uint8_t* data, size_t len) {
	digitalWrite(csPin, LOW);
	spi.beginTransaction(kSpiSettings);
	spi.transfer(addr | kWriteBurst);
	for (size_t i = 0; i < len; ++i)
		spi.transfer(data[i]);
	spi.endTransaction();
	digitalWrite(csPin, HIGH);
}

uint8_t readReg(SPIClass& spi, int8_t csPin, uint8_t addr) {
	digitalWrite(csPin, LOW);
	spi.beginTransaction(kSpiSettings);
	spi.transfer(addr | 0x80);  // R/W=1 -> read
	uint8_t v = spi.transfer(0x00);
	spi.endTransaction();
	digitalWrite(csPin, HIGH);
	return v;
}

// Verify each write; retry past a transient VCC droop on a marginal supply.
constexpr int kCfgWriteTries = 5;
void writeRegVerified(SPIClass& spi, int8_t csPin, uint8_t addr, uint8_t val) {
	for (int i = 0; i < kCfgWriteTries; ++i) {
		writeReg(spi, csPin, addr, val);
		if (readReg(spi, csPin, addr) == val)
			return;
	}
	Serial.printf("[rf] config reg 0x%02X stuck at 0x%02X != 0x%02X\n",
	              addr, readReg(spi, csPin, addr), val);
}

// OOK async-serial config (band-independent; FREQ/TEST0 set per band). From SmartRC-CC1101-Driver-Lib.
struct RegVal { uint8_t addr; uint8_t val; };
constexpr RegVal kCfgOOK[] = {
	{0x02, 0x0D},  // IOCFG0: GDO0 async serial data
	{0x08, 0x32},  // PKTCTRL0: async serial
	{0x07, 0x04},  // PKTCTRL1
	{0x0B, 0x06},  // FSCTRL1
	{0x0C, 0x00},  // FSCTRL0
	{0x10, 0x87},  // MDMCFG4: 203 kHz RX BW
	{0x11, 0x93},  // MDMCFG3: data rate
	{0x12, 0x30},  // MDMCFG2: ASK/OOK, no sync
	{0x13, 0x02},  // MDMCFG1
	{0x14, 0xF8},  // MDMCFG0
	{0x0A, 0x00},  // CHANNR
	{0x15, 0x47},  // DEVIATN
	{0x18, 0x18},  // MCSM0: auto-cal IDLE->RX/TX
	{0x19, 0x16},  // FOCCFG
	{0x1A, 0x1C},  // BSCFG
	{0x1B, 0xC7},  // AGCCTRL2: caps DVGA gain (quiets no-carrier noise)
	{0x1C, 0x00},  // AGCCTRL1
	{0x1D, 0xB2},  // AGCCTRL0
	{0x21, 0x56},  // FREND1
	{0x22, 0x11},  // FREND0: OOK
	{0x23, 0xE9},  // FSCAL3
	{0x24, 0x2A},  // FSCAL2
	{0x25, 0x00},  // FSCAL1
	{0x26, 0x1F},  // FSCAL0
	{0x29, 0x59},  // FSTEST
	{0x2C, 0x81},  // TEST2
	{0x2D, 0x35},  // TEST1
	{0x00, 0x29},  // IOCFG2
	{0x03, 0x47},  // FIFOTHR
};

// FREQ = carrier*65536/crystal; TEST0 picks the VCO band.
void configRadioOOK(SPIClass& spi, int8_t cs, double freqMHz) {
	for (const auto& r : kCfgOOK)
		writeRegVerified(spi, cs, r.addr, r.val);
	uint32_t word = static_cast<uint32_t>(freqMHz * 65536.0 / CC1101_XTAL_MHZ);
	writeRegVerified(spi, cs, 0x0D, static_cast<uint8_t>((word >> 16) & 0xFF));  // FREQ2
	writeRegVerified(spi, cs, 0x0E, static_cast<uint8_t>((word >> 8) & 0xFF));   // FREQ1
	writeRegVerified(spi, cs, 0x0F, static_cast<uint8_t>(word & 0xFF));          // FREQ0
	writeRegVerified(spi, cs, 0x2E, static_cast<uint8_t>(freqMHz < 350.0 ? 0x0B : 0x09));  // TEST0
	strobe(spi, cs, kSidle);
}

// On-demand RMT RX channel per radio's GDO0; intr_priority 3 (must match the Pixel group, see ir.cpp).
constexpr uint32_t kRfResolution   = 1000000;
constexpr size_t   kRfRxMemBlocks  = 48;        // non-DMA: multiple of 48 on the S3
constexpr uint32_t kRfRxGlitchNs   = 2000;      // source-clocked filter, max 255 ticks
// 20 ms idle > the inter-frame gap, so a whole press joins into one burst for the de-clip.
constexpr uint32_t kRfRxIdleNs     = 20000000;
constexpr size_t   kRfRxBufSymbols = 512;

// OOK PA tables: index 0 = off (space), index 1 = max power (mark).
constexpr size_t  kPaTableLen   = 8;
constexpr uint8_t kPaTable315[8] = {0x00, RF315_PA_POWER, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kPaTable433[8] = {0x00, RF433_PA_POWER, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

struct RfBand {
	SPIClass*            spi;
	int8_t               cs;
	gpio_num_t           pin;          // GDO0 data line
	const uint8_t*       paTable;
	rmt_channel_handle_t rx;
	bool                 rxEnabled;
	volatile bool        frameReady;
	volatile size_t      frameSymbols;
	rmt_symbol_word_t    buf[kRfRxBufSymbols];
};

RfBand s_band315 = { &spi315, kCs315, static_cast<gpio_num_t>(kGdo0_315), kPaTable315 };
RfBand s_band433 = { &spi433, kCs433, static_cast<gpio_num_t>(kGdo0_433), kPaTable433 };

RfBand* bandFor(uint16_t freqMHz) {
	if (freqMHz == 315) return &s_band315;
	if (freqMHz == 433) return &s_band433;
	return nullptr;
}

bool IRAM_ATTR rfRxDoneCb(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata, void* ctx) {
	RfBand* b = static_cast<RfBand*>(ctx);
	b->frameSymbols = edata->num_symbols;
	b->frameReady = true;
	return false;
}

rmt_receive_config_t makeRfRxConfig() {
	rmt_receive_config_t c = {};
	c.signal_range_min_ns = kRfRxGlitchNs;
	c.signal_range_max_ns = kRfRxIdleNs;
	c.flags.en_partial_rx = 0;
	return c;
}

// On-demand RMT TX channel on the radio's GDO0, freed after the send. OOK has no carrier.
constexpr size_t   kRfTxMemBlocks = 48;
constexpr size_t   kRfMaxSymbols  = config::MAX_PULSES / 2;
rmt_symbol_word_t  s_rfTxBuf[kRfMaxSymbols];

void armBand(RfBand& b) {
	if (!b.rx) {
		rmt_rx_channel_config_t cfg = {};
		cfg.gpio_num = b.pin;
		cfg.clk_src = RMT_CLK_SRC_DEFAULT;
		cfg.resolution_hz = kRfResolution;
		cfg.mem_block_symbols = kRfRxMemBlocks;
		cfg.intr_priority = 3;
		esp_err_t e = rmt_new_rx_channel(&cfg, &b.rx);
		if (e != ESP_OK) {
			Serial.printf("[rf] rmt_new_rx_channel failed: %s\n", esp_err_to_name(e));
			b.rx = nullptr;
			return;
		}
		rmt_rx_event_callbacks_t cbs = {};
		cbs.on_recv_done = rfRxDoneCb;
		rmt_rx_register_event_callbacks(b.rx, &cbs, &b);  // ctx = this band
	}
	if (!b.rxEnabled) {
		if (rmt_enable(b.rx) != ESP_OK)
			return;
		b.rxEnabled = true;
	}
	strobe(*b.spi, b.cs, kSrx);
	b.frameReady = false;
	rmt_receive_config_t c = makeRfRxConfig();
	esp_err_t e = rmt_receive(b.rx, b.buf, sizeof(b.buf), &c);
	if (e != ESP_OK)
		Serial.printf("[rf] rmt_receive failed: %s\n", esp_err_to_name(e));
}

void disarmBand(RfBand& b) {
	strobe(*b.spi, b.cs, kSidle);
	if (b.rxEnabled) {
		rmt_disable(b.rx);
		b.rxEnabled = false;
	}
	if (b.rx) {
		rmt_del_channel(b.rx);
		b.rx = nullptr;
	}
	b.frameReady = false;
}

// Collapse one captured OOK envelope into a us mark/space burst (GDO0 high = mark);
// starts on the first mark, drops a trailing space so it ends on a mark (odd). Re-arms RX.
bool collapseBand(RfBand& b, uint16_t* out, size_t cap, size_t* outLen) {
	if (!b.rx || !b.frameReady)
		return false;

	size_t n            = b.frameSymbols;
	size_t outN         = 0;
	bool   started      = false;
	bool   lastWasSpace = false;

	for (size_t i = 0; i < n && outN < cap; ++i) {
		uint32_t durs[2] = { b.buf[i].duration0, b.buf[i].duration1 };
		uint8_t  lvls[2] = { static_cast<uint8_t>(b.buf[i].level0),
		                     static_cast<uint8_t>(b.buf[i].level1) };
		bool done = false;
		for (int h = 0; h < 2 && outN < cap; ++h) {
			uint32_t dur = durs[h];
			if (dur == 0) { done = true; break; }  // end-of-frame filler
			if (!started) {
				if (lvls[h] != 1)
					continue;  // pre-mark gap
				started = true;
			}
			out[outN++] = dur > 65535 ? 65535 : static_cast<uint16_t>(dur);
			lastWasSpace = (lvls[h] == 0);
		}
		if (done)
			break;
	}
	if (lastWasSpace && outN > 0)
		--outN;

	b.frameReady = false;
	rmt_receive_config_t c = makeRfRxConfig();
	rmt_receive(b.rx, b.buf, sizeof(b.buf), &c);

	if (outN == 0)
		return false;
	*outLen = outN;
	return true;
}

bool transmitOOK(RfBand& b, const RFCode& code) {
	writeBurst(*b.spi, b.cs, kRegPatable, b.paTable, kPaTableLen);
	// Idle first so the CC1101 isn't driving GDO0 when the RMT TX channel takes the pad (never pinMode it).
	strobe(*b.spi, b.cs, kSidle);

	rmt_tx_channel_config_t cfg = {};
	cfg.gpio_num = b.pin;
	cfg.clk_src = RMT_CLK_SRC_DEFAULT;
	cfg.resolution_hz = kRfResolution;
	cfg.mem_block_symbols = kRfTxMemBlocks;
	cfg.trans_queue_depth = 4;
	cfg.intr_priority = 3;  // must match the Pixel RMT group
	rmt_channel_handle_t tx = nullptr;
	esp_err_t e = rmt_new_tx_channel(&cfg, &tx);
	if (e != ESP_OK) {
		Serial.printf("[rf] rmt_new_tx_channel failed: %s\n", esp_err_to_name(e));
		return false;
	}
	rmt_encoder_handle_t enc = nullptr;
	rmt_copy_encoder_config_t ec = {};
	e = rmt_new_copy_encoder(&ec, &enc);
	if (e != ESP_OK) {
		rmt_del_channel(tx);
		return false;
	}
	e = rmt_enable(tx);
	if (e != ESP_OK) {
		rmt_del_encoder(enc);
		rmt_del_channel(tx);
		return false;
	}

	size_t symbols = packPulsesToSymbols(code.pulses, code.length, s_rfTxBuf, kRfMaxSymbols);
	bool sent = false;
	if (symbols > 0) {
		strobe(*b.spi, b.cs, kStx);  // MCSM0=0x18 auto-calibrates IDLE->TX
		// Let the VCO auto-cal (~0.8 ms) finish before keying, or PA spikes smear the carrier.
		delayMicroseconds(1200);
		rmt_transmit_config_t tc = {};
		tc.loop_count = 0;
		tc.flags.eot_level = 0;  // idle low after the frame -> PA off
		e = rmt_transmit(tx, enc, s_rfTxBuf, symbols * sizeof(rmt_symbol_word_t), &tc);
		if (e == ESP_OK) {
			rmt_tx_wait_all_done(tx, 1000);
			sent = true;
		}
	}

	strobe(*b.spi, b.cs, kSidle);
	rmt_disable(tx);
	rmt_del_encoder(enc);
	rmt_del_channel(tx);
	return sent;
}

}  // namespace

void initRadios() {
	bringUpBus(spi315, kSck315, kMiso315, kMosi315, kCs315);
	bringUpBus(spi433, kSck433, kMiso433, kMosi433, kCs433);
	resetRadio(spi315, kCs315, kMiso315);
	resetRadio(spi433, kCs433, kMiso433);
	configRadioOOK(spi315, kCs315, RF315_FREQ_MHZ);
	configRadioOOK(spi433, kCs433, RF433_FREQ_MHZ);
}

void startRFRx(uint16_t freqMHz) {
	if (RfBand* b = bandFor(freqMHz))
		armBand(*b);
}

void stopRFRx(uint16_t freqMHz) {
	if (RfBand* b = bandFor(freqMHz))
		disarmBand(*b);
}

bool pollRFBurst(uint16_t freqMHz, uint16_t* out, size_t cap, size_t* outLen) {
	RfBand* b = bandFor(freqMHz);
	return b ? collapseBand(*b, out, cap, outLen) : false;
}

bool sendRFCode(uint16_t freqMHz, const RFCode& code) {
	if (!code.pulses || code.length < 2 || (code.length & 1))
		return false;
	RfBand* b = bandFor(freqMHz);
	if (!b) {  // only 315 and 433 are configured
		Serial.printf("[rf] sendRFCode: unsupported band %u MHz\n", freqMHz);
		return false;
	}
	if (b->rx) {  // a learn capture holds the RMT slot; can't TX while receiving
		Serial.println("[rf] sendRFCode: RF RX active, TX skipped");
		return false;
	}
	return transmitOOK(*b, code);
}
