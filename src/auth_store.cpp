#include "auth_store.h"

#include <nvs_flash.h>

#include <utility>

namespace runtime {

namespace {
constexpr char NVS_NAMESPACE[] = "rfirauth";  // separate from config's "rfirblaster"
constexpr char KEY_CRED[]      = "cred";      // PBKDF2 password record
constexpr char KEY_NONCE[]     = "nonce";     // one-time setup nonce
constexpr char KEY_CODE[]      = "setupcode"; // plaintext HomeKit pairing code, for display

bool readStr(nvs_handle_t h, const char* key, std::string& out) {
	size_t len = 0;
	if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0)
		return false;
	std::string buf(len, '\0');
	if (nvs_get_str(h, key, &buf[0], &len) != ESP_OK)
		return false;  // out untouched
	buf.resize(len - 1);  // len counts the NUL terminator
	out = std::move(buf);
	return true;
}
}  // namespace

AuthStore::AuthStore() {
	// Static-init order vs HomeSpan is undefined, so init the partition ourselves; idempotent.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		nvs_flash_init();
	}
	ok_ = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle_) == ESP_OK;
}

AuthStore::~AuthStore() {
	if (ok_)
		nvs_close(handle_);
}

bool AuthStore::hasCredential() {
	if (!ok_)
		return false;
	size_t len = 0;
	return nvs_get_str(handle_, KEY_CRED, nullptr, &len) == ESP_OK && len > 1;
}

bool AuthStore::getCredential(std::string& out) {
	return ok_ && readStr(handle_, KEY_CRED, out);
}

bool AuthStore::setCredential(const std::string& record) {
	if (!ok_)
		return false;
	if (nvs_set_str(handle_, KEY_CRED, record.c_str()) != ESP_OK)
		return false;
	return nvs_commit(handle_) == ESP_OK;
}

bool AuthStore::getNonce(std::string& out) {
	return ok_ && readStr(handle_, KEY_NONCE, out);
}

bool AuthStore::setNonce(const std::string& nonce) {
	if (!ok_)
		return false;
	if (nvs_set_str(handle_, KEY_NONCE, nonce.c_str()) != ESP_OK)
		return false;
	return nvs_commit(handle_) == ESP_OK;
}

void AuthStore::clearNonce() {
	if (!ok_)
		return;
	nvs_erase_key(handle_, KEY_NONCE);
	nvs_commit(handle_);
}

bool AuthStore::getSetupCode(std::string& out) {
	return ok_ && readStr(handle_, KEY_CODE, out);
}

bool AuthStore::setSetupCode(const std::string& code) {
	if (!ok_)
		return false;
	if (nvs_set_str(handle_, KEY_CODE, code.c_str()) != ESP_OK)
		return false;
	return nvs_commit(handle_) == ESP_OK;
}

void AuthStore::eraseAll() {
	if (!ok_)
		return;
	nvs_erase_key(handle_, KEY_CRED);
	nvs_erase_key(handle_, KEY_NONCE);
	nvs_erase_key(handle_, KEY_CODE);
	nvs_commit(handle_);
}

}  // namespace runtime
