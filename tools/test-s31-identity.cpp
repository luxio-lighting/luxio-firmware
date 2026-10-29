#include <psa/crypto.h>
#include <mbedtls/pk.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
static mbedtls_pk_context identity;
static std::string publicKey;
static struct { char device_id[65] = {}; } state;
static struct {
  std::vector<uint8_t> bytes;
  size_t getBytesLength(const char *) { return bytes.size(); }
  void getBytes(const char *, void *out, size_t n) { assert(n == bytes.size()); memcpy(out, bytes.data(), n); }
  size_t putBytes(const char *, const void *in, size_t n) { auto p = static_cast<const uint8_t *>(in); bytes.assign(p, p + n); return n; }
} prefs;
static void bootIdentity() {
// PRODUCTION_IDENTITY
}
int main() {
  assert(psa_crypto_init() == PSA_SUCCESS);
  bootIdentity();
  const auto key = prefs.bytes;
  const auto pub = std::vector<char>(publicKey.begin(), publicKey.end());
  const std::string id = state.device_id;
  assert(!key.empty() && id.size() == 64 && !pub.empty());
  mbedtls_pk_free(&identity); bootIdentity();
  assert(prefs.bytes == key && publicKey == std::string(pub.begin(), pub.end()) && state.device_id == id);
  uint8_t digest[32], signature[80]; size_t length = 0;
  assert(!mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const uint8_t *>("Luxio"), 5, digest));
  assert(!mbedtls_pk_sign(&identity, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, sizeof(signature), &length));
  assert(!mbedtls_pk_verify(&identity, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, length));
  digest[0] ^= 1;
  assert(mbedtls_pk_verify(&identity, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, length));
  mbedtls_pk_free(&identity);
  puts("PASS: S31 PSA identity generation, DER persistence, stable public identity and signing");
}
