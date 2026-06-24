#include "ui.h"

#include "HomeSpan.h"

#include "board_pins.h"

namespace {
constexpr int kPixelPin = PIN_STATUS_LED;
Pixel*        g_pixel      = nullptr;
UiState       g_state      = UiState::Normal;
bool          g_ledEnabled = true;
int           g_lastKey    = -1;

// HomeSpan never clears HS_OTA_STARTED after a failed OTA (its error path only logs), so the LED sticks.
HS_STATUS          g_lastRealStatus = HS_WIFI_NEEDED;
constexpr uint32_t kOtaStaleSec     = 90;

// Collapse paired and connected to one green so the LED doesn't flicker as controllers come and go.
Pixel::Color statusColor(HS_STATUS s) {
	switch (s) {
		case HS_PAIRING_NEEDED: return Pixel::RGB(0, 255, 255);
		case HS_PAIRED:
		case HS_CONNECTED:      return Pixel::RGB(0, 255, 0);
		default:                return Pixel::RGB(255, 0, 255);  // booting / WiFi / AP
	}
}

void render() {
	if (!g_pixel)
		return;
	int          key;
	Pixel::Color c;
	switch (g_state) {  // recovery states override the HomeKit status color
		case UiState::SafeModeArmed: c = Pixel::RGB(255, 120, 0); key = 1; break;
		case UiState::FactoryArmed:  c = Pixel::RGB(255, 0, 0);   key = 2; break;
		case UiState::SafeMode:      c = Pixel::RGB(0, 60, 255);  key = 3; break;
		default: {
			std::pair<HS_STATUS, uint32_t> st = homeSpan.getStatus();
			HS_STATUS eff = st.first;
			if (st.first == HS_OTA_STARTED) {
				if (st.second >= kOtaStaleSec)  // success reboots, so a long-lived HS_OTA_STARTED means it failed
					eff = g_lastRealStatus;
			} else {
				g_lastRealStatus = st.first;
			}
			bool healthy = (eff == HS_PAIRED || eff == HS_CONNECTED);
			if (healthy) {  // one key for both, so it never re-draws between paired and connected
				c   = g_ledEnabled ? statusColor(eff) : Pixel::RGB(0, 0, 0);
				key = g_ledEnabled ? 201 : 200;
			} else {
				c   = statusColor(eff);
				key = 100 + static_cast<int>(eff);
			}
			break;
		}
	}
	if (key != g_lastKey) {  // re-drive only on a state change, not every loop
		g_pixel->set(c);
		g_lastKey = key;
	}
}
}  // namespace

void initUI() {
	g_pixel = new Pixel(kPixelPin, STATUS_LED_COLOR_ORDER);  // color order is board-specific
	setUiState(UiState::Normal);  // paint status now; HomeSpan's blocking AP loop never reaches updateUI()
}

void setUiState(UiState s) {
	g_state = s;
	render();
}

void setLedEnabled(bool enabled) {
	g_ledEnabled = enabled;
	render();
}

void updateUI() {
	render();
}
