#pragma once

namespace onboarding {

// 4-char HAP Setup ID in the pairing QR; main.cpp passes the same to setQRID() (must match).
constexpr char kQrSetupId[] = "RFIR";

// First-boot captive portal for homeSpan.setApFunction() (called when no Wi-Fi creds
// exist): open time-boxed AP, captures Wi-Fi + pairing code, persists, reboots.
// Blocks and never returns.
void run();

}  // namespace onboarding
