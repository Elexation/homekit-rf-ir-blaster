#pragma once

#include <string>

namespace web {

// EC P-256 self-signed cert + key, PEM. No trailing NUL stored; the esp_tls boundary
// passes c_str() with size()+1.
struct CertMaterial {
	std::string certPem;
	std::string keyPem;
	bool        ok = false;
};

// Returns the stored cert, creating + persisting one on first call (own NVS namespace).
// Blocks briefly on first-boot keygen. ok=false only on unrecoverable NVS/crypto failure.
CertMaterial loadOrCreateCert();

}  // namespace web
