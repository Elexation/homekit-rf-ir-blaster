#pragma once

// Default GPIO + RF config. Override any value with a -D build flag in platformio.ini
// to retarget a board (no source edit). Defaults match the Waveshare ESP32-S3-Zero.

#ifndef PIN_RF315_SCK
#define PIN_RF315_SCK 12
#endif
#ifndef PIN_RF315_MISO
#define PIN_RF315_MISO 13
#endif
#ifndef PIN_RF315_MOSI
#define PIN_RF315_MOSI 11
#endif
#ifndef PIN_RF315_CS
#define PIN_RF315_CS 10
#endif
#ifndef PIN_RF315_GDO0
#define PIN_RF315_GDO0 1
#endif

#ifndef PIN_RF433_SCK
#define PIN_RF433_SCK 4
#endif
#ifndef PIN_RF433_MISO
#define PIN_RF433_MISO 5
#endif
#ifndef PIN_RF433_MOSI
#define PIN_RF433_MOSI 6
#endif
#ifndef PIN_RF433_CS
#define PIN_RF433_CS 7
#endif
#ifndef PIN_RF433_GDO0
#define PIN_RF433_GDO0 8
#endif

#ifndef PIN_IR_TX
#define PIN_IR_TX 9
#endif
#ifndef PIN_IR_RX
#define PIN_IR_RX 2
#endif

#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED 21
#endif
#ifndef STATUS_LED_COLOR_ORDER
#define STATUS_LED_COLOR_ORDER "RGB"
#endif

#ifndef CC1101_XTAL_MHZ
#define CC1101_XTAL_MHZ 26.0
#endif
#ifndef RF315_FREQ_MHZ
#define RF315_FREQ_MHZ 315.0
#endif
#ifndef RF433_FREQ_MHZ
#define RF433_FREQ_MHZ 433.92
#endif
#ifndef RF315_PA_POWER
#define RF315_PA_POWER 0xC2
#endif
#ifndef RF433_PA_POWER
#define RF433_PA_POWER 0xC0
#endif
