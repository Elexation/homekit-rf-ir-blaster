#include "cert_store.h"

#include <nvs.h>
#include <nvs_flash.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include <cstring>
#include <utility>
#include <vector>

namespace web {

namespace {
constexpr char NVS_NAMESPACE[] = "rfircert";  // distinct from config + auth namespaces
constexpr char KEY_CERT[]      = "cert";
constexpr char KEY_KEY[]       = "key";

// Clock-independent validity: the ESP32 has no RTC (boots at 1970 until NTP), so a wide
// near-epoch..far-future window keeps the cert valid regardless of clock.
constexpr char NOT_BEFORE[]   = "20200101000000";
constexpr char NOT_AFTER[]    = "20991231235959";
constexpr char SUBJECT_NAME[] = "CN=RF-IR Blaster";

bool nvsReadStr(nvs_handle_t h, const char* key, std::string& out) {
	size_t len = 0;
	if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len <= 1)
		return false;
	std::string buf(len, '\0');
	if (nvs_get_str(h, key, &buf[0], &len) != ESP_OK)
		return false;
	buf.resize(len - 1);  // len counts the NUL terminator
	out = std::move(buf);
	return true;
}

bool generate(std::string& certPemOut, std::string& keyPemOut) {
	mbedtls_pk_context        key;
	mbedtls_x509write_cert    crt;
	mbedtls_ctr_drbg_context  ctrDrbg;
	mbedtls_entropy_context   entropy;

	mbedtls_pk_init(&key);
	mbedtls_x509write_crt_init(&crt);
	mbedtls_ctr_drbg_init(&ctrDrbg);
	mbedtls_entropy_init(&entropy);

	bool          ok  = false;
	const char*   pers = "rfir_cert";
	unsigned char serial[] = {0x01};
	std::vector<unsigned char> certBuf(1536);  // off-stack: keygen already strains the task stack
	std::vector<unsigned char> keyBuf(1024);

	do {
		if (mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
		                          (const unsigned char*)pers, strlen(pers)) != 0)
			break;
		if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0)
			break;
		if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
		                        mbedtls_ctr_drbg_random, &ctrDrbg) != 0)
			break;

		mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
		mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
		mbedtls_x509write_crt_set_subject_key(&crt, &key);
		mbedtls_x509write_crt_set_issuer_key(&crt, &key);  // self-signed: issuer == subject
		if (mbedtls_x509write_crt_set_subject_name(&crt, SUBJECT_NAME) != 0)
			break;
		if (mbedtls_x509write_crt_set_issuer_name(&crt, SUBJECT_NAME) != 0)
			break;
		if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) != 0)
			break;
		if (mbedtls_x509write_crt_set_validity(&crt, NOT_BEFORE, NOT_AFTER) != 0)
			break;
		if (mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1) != 0)  // not a CA
			break;

		if (mbedtls_x509write_crt_pem(&crt, certBuf.data(), certBuf.size(),
		                              mbedtls_ctr_drbg_random, &ctrDrbg) != 0)
			break;
		if (mbedtls_pk_write_key_pem(&key, keyBuf.data(), keyBuf.size()) != 0)
			break;

		certPemOut = reinterpret_cast<const char*>(certBuf.data());  // NUL-terminated by mbedtls
		keyPemOut  = reinterpret_cast<const char*>(keyBuf.data());
		ok = true;
	} while (false);

	mbedtls_entropy_free(&entropy);
	mbedtls_ctr_drbg_free(&ctrDrbg);
	mbedtls_x509write_crt_free(&crt);
	mbedtls_pk_free(&key);
	return ok;
}
}  // namespace

CertMaterial loadOrCreateCert() {
	CertMaterial m;

	// Static-init order vs HomeSpan is undefined, so init the partition ourselves; idempotent.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		nvs_flash_init();
	}
	nvs_handle_t h;
	if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
		return m;

	if (nvsReadStr(h, KEY_CERT, m.certPem) && nvsReadStr(h, KEY_KEY, m.keyPem)) {
		m.ok = true;
		nvs_close(h);
		return m;
	}

	std::string certPem, keyPem;
	if (!generate(certPem, keyPem)) {
		nvs_close(h);
		return m;
	}
	// Persist for later boots; a persist failure still lets this boot serve the RAM pair.
	if (nvs_set_str(h, KEY_CERT, certPem.c_str()) == ESP_OK &&
	    nvs_set_str(h, KEY_KEY, keyPem.c_str()) == ESP_OK)
		nvs_commit(h);
	nvs_close(h);

	m.certPem = std::move(certPem);
	m.keyPem  = std::move(keyPem);
	m.ok      = true;
	return m;
}

}  // namespace web
