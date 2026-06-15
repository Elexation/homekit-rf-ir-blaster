#pragma once

#include <nvs.h>

#include <string>

namespace runtime {

// NVS persistence for the two secrets Config excludes: the PBKDF2 password record
// and the one-time setup nonce. Own namespace; device-only.
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

	void eraseAll();  // factory reset: drop credential + nonce together

private:
	nvs_handle_t handle_ = 0;
	bool         ok_     = false;
};

}  // namespace runtime
