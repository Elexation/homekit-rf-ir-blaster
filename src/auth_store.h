#pragma once

#include <nvs.h>

#include <string>

namespace runtime {

// NVS for the device-local values Config excludes: PBKDF2 password record, setup
// nonce, plaintext pairing code. Own namespace.
class AuthStore {
public:
	AuthStore();
	~AuthStore();

	bool ok() const { return ok_; }

	bool hasCredential();
	bool getCredential(std::string& out);  // false if absent; out untouched
	bool setCredential(const std::string& record);

	bool getNonce(std::string& out);  // false if absent; out untouched
	bool setNonce(const std::string& nonce);
	void clearNonce();

	// Plaintext pairing code, device-local so the config UI can show it (HomeSpan
	// keeps only the SRP verifier).
	bool getSetupCode(std::string& out);  // false if absent; out untouched
	bool setSetupCode(const std::string& code);

	// Plaintext OTA password (HomeSpan stores only the hash) so the config UI can show it.
	bool getOtaPassword(std::string& out);  // false if absent; out untouched
	bool setOtaPassword(const std::string& pw);

	void eraseAll();  // factory reset: drop credential, nonce, setup code, OTA password together

private:
	nvs_handle_t handle_ = 0;
	bool         ok_     = false;
};

// 128-bit random as 32 hex chars (HomeSpan caps the OTA password at 32).
std::string makeOtaPassword();

}  // namespace runtime
