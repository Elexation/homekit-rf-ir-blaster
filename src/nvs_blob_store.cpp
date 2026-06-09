#include "nvs_blob_store.h"

#include <nvs_flash.h>

#include <utility>

namespace config {

namespace {
constexpr char NVS_NAMESPACE[] = "rfirblaster";  // distinct from HomeSpan's CHAR/WIFI/OTA/SRP/HAP
constexpr char NVS_KEY[]       = "config";       // single whole-config blob
}  // namespace

NvsBlobStore::NvsBlobStore() {
	// Static-init order vs HomeSpan is undefined, so init the partition ourselves; idempotent.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		nvs_flash_init();
	}
	ok_ = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle_) == ESP_OK;
}

NvsBlobStore::~NvsBlobStore() {
	if (ok_)
		nvs_close(handle_);
}

bool NvsBlobStore::get(std::vector<uint8_t>& out) {
	if (!ok_)
		return false;
	size_t len = 0;
	if (nvs_get_blob(handle_, NVS_KEY, nullptr, &len) != ESP_OK || len == 0)
		return false;
	std::vector<uint8_t> buf(len);
	if (nvs_get_blob(handle_, NVS_KEY, buf.data(), &len) != ESP_OK)
		return false;  // out untouched
	out = std::move(buf);
	return true;
}

bool NvsBlobStore::put(const uint8_t* data, size_t len) {
	if (!ok_)
		return false;
	// On failure (e.g. ESP_ERR_NVS_NOT_ENOUGH_SPACE) the prior committed blob is retained.
	if (nvs_set_blob(handle_, NVS_KEY, data, len) != ESP_OK)
		return false;
	return nvs_commit(handle_) == ESP_OK;
}

void NvsBlobStore::erase() {
	if (!ok_)
		return;
	nvs_erase_key(handle_, NVS_KEY);
	nvs_commit(handle_);
}

}  // namespace config
