# homekit-rf-ir-blaster

A native Apple HomeKit accessory firmware for the ESP32-S3 that transmits and learns sub-GHz RF and infrared. It pairs directly with the Apple Home app with no external bridge needed.

Control is over HomeKit only (encrypted and authenticated by HAP). Configuration is over a local, password-protected web UI served by the device.

## Features

- Dual-band sub-GHz RF (315 and 433 MHz) using two CC1101 radios, each permanently tuned to one band, so a learn listens on both at once and auto-detects the band.
- Infrared transmit with carrier detection on capture and matched-carrier playback.
- On-device learning for RF and IR, with rolling codes refused at learn time.
- Config-driven HomeKit accessories (Switch, Outlet, Light Bulb, Fan, Window Covering, Television), added and named from the web UI.
- Self-signed HTTPS config UI with login, CSRF protection, rate-limiting, and config export/import.
- OTA updates with a per-device password, image validation, and automatic rollback on an early-boot crash.
- Captive-portal onboarding for first-boot WiFi and HomeKit pairing; physical safe-mode and factory-reset recovery.

## Hardware

The reference build targets the Waveshare ESP32-S3-Zero (ESP32-S3FH4R2). Every GPIO and RF parameter is overridable with a build flag, so a different board, pin map, crystal, or band is a configuration change with no source edits.

### Bill of materials (reference build)

- 1x ESP32-S3 board with native USB
- 2x CC1101 transceiver modules (one per band), each with a matching 315 MHz and 433 MHz quarter-wave antenna
- 1x carrier-preserving IR receiver (TSMP96000), which outputs the raw carrier so the firmware can measure each remote's carrier frequency
- 940 nm IR LEDs driven from the IR transmit line, optionally through a logic-level N-channel MOSFET per bank
- clean 5 V USB supply

The firmware drives a single IR transmit output; multiple banks wired to it fire together.

### Default pin map (Waveshare ESP32-S3-Zero)

| Function | GPIO |
|---|---|
| RF 315 SCK / MISO / MOSI / CS / GDO0 | 12 / 13 / 11 / 10 / 1 |
| RF 433 SCK / MISO / MOSI / CS / GDO0 | 4 / 5 / 6 / 7 / 8 |
| IR transmit | 9 |
| IR receive | 2 |
| WS2812 status LED | 21 (onboard) |
| Recovery button | 0 (onboard BOOT) |

The CC1101 modules are 3.3 V only, and each radio uses its own SPI bus. The status LED is optional (status only) and onboard on the reference board; omit it and you lose only the visual status.

### Retargeting with build flags

Override any default in `platformio.ini` under `build_flags`. Example:

```ini
build_flags =
  -D PIN_IR_TX=18
  -D PIN_STATUS_LED=48
  -D STATUS_LED_COLOR_ORDER=\"GRB\"
  -D CC1101_XTAL_MHZ=27.0
```

| Flag | Default | Meaning |
|---|---|---|
| `PIN_RF315_SCK` / `_MISO` / `_MOSI` / `_CS` / `_GDO0` | 12 / 13 / 11 / 10 / 1 | 315 MHz radio SPI + data pins |
| `PIN_RF433_SCK` / `_MISO` / `_MOSI` / `_CS` / `_GDO0` | 4 / 5 / 6 / 7 / 8 | 433 MHz radio SPI + data pins |
| `PIN_IR_TX` / `PIN_IR_RX` | 9 / 2 | IR transmit and receive pins |
| `PIN_STATUS_LED` | 21 | WS2812 status LED pin |
| `STATUS_LED_COLOR_ORDER` | `"RGB"` | WS2812 byte order (some boards are `"GRB"`) |
| `CC1101_XTAL_MHZ` | 26.0 | CC1101 crystal frequency |
| `RF315_FREQ_MHZ` / `RF433_FREQ_MHZ` | 315.0 / 433.92 | Per-band carrier frequency |
| `RF315_PA_POWER` / `RF433_PA_POWER` | 0xC2 / 0xC0 | Per-band PA table value (output power) |

The two radios are fixed at 315 and 433 MHz, which covers most consumer fixed-code OOK remotes. The frequency flags retune a slot; running an entirely different band end to end is not supported.

## Build and flash

Built with [PlatformIO](https://platformio.org/); the board id is `lolin_s3_mini`.

- Build: `pio run -e lolin_s3_mini`
- Flash over USB, then monitor: `pio run -e lolin_s3_mini -t upload` then `pio device monitor -b 115200`

The first flash must go over USB and uses the custom `partitions.csv` (no SPIFFS, dual OTA slots), which cannot be changed over the air, so it has to be right before the first OTA. Web UI assets are bundled into the firmware at build time.

## First-time setup

On a board with no stored WiFi credentials, the device starts an open, time-boxed SoftAP and serves a captive-portal wizard:

1. Join the device's WiFi network; the wizard opens automatically.
2. Choose your home WiFi and enter its password; the device connects live to verify it.
3. Set the HomeKit pairing code (a random code is pre-filled; you can override it).
4. The done screen shows the pairing code, a pairing QR, a one-time setup nonce, and the device's address on your network, then reboots onto your WiFi.

Then pair in the Apple Home app (scan the QR or enter the code), browse to the device over HTTPS, enter the one-time nonce, and set the web admin password. The admin password is only set over HTTPS, never over the open SoftAP.

## Usage

Open the device's address over HTTPS (the self-signed certificate gives a one-time browser warning) and log in. From the config UI you can:

- Add a device, choose its HomeKit type, and name it.
- Learn a code: arm a capture, press the original remote a few times, and bind it to a command. The band and IR carrier are detected automatically.
- Set per-command repeat behavior, toggle the status LED, and export or import the configuration as a backup.

## Over-the-air updates

OTA uses a per-device password (auto-generated on first boot, shown read-only and regenerable on the settings page). To push a build (the bundled PlatformIO Python provides `espota.py`):

```sh
espota.py -i <device-ip> -p 3232 -a <password> -f .pio/build/lolin_s3_mini/firmware.bin
```

On Windows the device connects back to the host to pull the image, so allow the PlatformIO Python through the firewall, and pass `-I <host-lan-ip>` if the host has virtual network adapters. Images are validated before install, and one that crashes early in boot reverts to the previous slot. Always USB-test a build before pushing it over the air.

## Recovery

Recovery is physical, since a bad network setting can lock out the web UI:

- Hold the onboard BOOT button while running: about 5 seconds enters safe mode, about 10 seconds triggers a factory reset.
- Power-cycle three times to enter safe mode on a sealed unit with no button access.
- Hold BOOT while resetting to enter USB flash mode.

Safe mode boots the web UI on known-good network defaults while keeping your devices and codes. Factory reset wipes everything, including pairing and WiFi credentials, returning the next boot to onboarding.

## License

Released under the [MIT License](LICENSE).
