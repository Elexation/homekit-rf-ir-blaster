#include "ui.h"

#include "HomeSpan.h"

#include "board_pins.h"

namespace {
constexpr int kPixelPin = PIN_STATUS_LED;
Pixel*        g_pixel      = nullptr;
UiState       g_state      = UiState::Normal;
bool          g_ledEnabled = true;
int           g_lastKey    = -1;

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
			HS_STATUS s = homeSpan.getStatus().first;
			bool healthy = (s == HS_PAIRED || s == HS_CONNECTED);
			if (healthy) {  // one key for both, so it never re-draws between paired and connected
				c   = g_ledEnabled ? statusColor(s) : Pixel::RGB(0, 0, 0);
				key = g_ledEnabled ? 201 : 200;
			} else {
				c   = statusColor(s);
				key = 100 + static_cast<int>(s);
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
