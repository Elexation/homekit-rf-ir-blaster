# homekit-rf-ir-blaster

A native Apple HomeKit accessory built on the ESP32-S3 that transmits and learns both sub-GHz RF and infrared. It pairs directly with the Apple Home app, with no Homebridge, Home Assistant, or cloud bridge in between, and captures new codes on-device.

## Features

- Dual-band sub-GHz RF (315 MHz and 433 MHz) using two dedicated CC1101 radios, each permanently tuned to one band
- Infrared transmit with carrier-frequency detection on capture and matched-carrier playback
- On-device learning for both RF and IR, with fixed-code versus rolling-code detection
- Three independently driven IR LED banks (front and both sides) for wide room coverage
- WiFi-only, provisioned through HomeSpan's captive portal or serial CLI, with credentials stored in NVS
- Addressable WS2812 RGB status LED; setup and configuration over a local web UI

## Firmware

- [HomeSpan](https://github.com/HomeSpan/HomeSpan) for the HomeKit Accessory Protocol
- Arduino-ESP32 core via the [pioarduino](https://github.com/pioarduino/platform-espressif32) PlatformIO platform
- ESP-IDF RMT peripheral for RF and IR transmit and capture

## License

Released under the [MIT License](LICENSE).
