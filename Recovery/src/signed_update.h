#pragma once
#include "update_job.h"
#include "ota_public_key.h"
#include <cJSON.h>
#include <cstdio>
#include <mbedtls/pk.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#ifndef LUXIO_CLOUD_URL
#define LUXIO_CLOUD_URL "http://api.luxio.lighting"
#endif

inline bool verifySignedUpdate(const UpdateJob &job, uint32_t minimumBuild, uint32_t maximumChannel = 100) {
  if (!validUpdateJob(job) || job.build <= minimumBuild || job.channel > maximumChannel || strcmp(job.keyId, LUXIO_OTA_KEY_ID)) return false;
  char expectedUrl[sizeof(job.url)];
  int urlLength = snprintf(expectedUrl, sizeof(expectedUrl), "%s/firmware/%s", LUXIO_CLOUD_URL, job.sha256);
  if (urlLength < 0 || size_t(urlLength) >= sizeof(expectedUrl) || strcmp(job.url, expectedUrl)) return false;
  char payload[256];
  int length = snprintf(payload, sizeof(payload), "luxio-ota-v1\n%s\n%s\n%s\n%lu\n%lu\n%lu\n%s\n",
    job.keyId, CONFIG_IDF_TARGET, job.target, (unsigned long)job.build, (unsigned long)job.channel, (unsigned long)job.size, job.sha256);
  if (length < 0 || size_t(length) >= sizeof(payload)) return false;
  uint8_t digest[32], signature[72]; size_t signatureLength = 0;
  if (mbedtls_base64_decode(signature, sizeof(signature), &signatureLength,
      reinterpret_cast<const uint8_t *>(job.signature), strlen(job.signature)) || signatureLength < 68 ||
      mbedtls_sha256(reinterpret_cast<const uint8_t *>(payload), length, digest, 0)) return false;
  mbedtls_pk_context key; mbedtls_pk_init(&key);
  int result = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const uint8_t *>(LUXIO_OTA_PUBLIC_KEY), sizeof(LUXIO_OTA_PUBLIC_KEY));
  if (!result) result = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, signatureLength);
  mbedtls_pk_free(&key);
  return result == 0;
}

// cJSON's SDK nesting limit is too large for an embedded task stack.
inline cJSON *parseUpdateJson(const char *text, size_t length) {
  if (!text || !length || length > 6144 || memchr(text, 0, length)) return nullptr;
  unsigned depth = 0; bool quoted = false, escaped = false;
  for (size_t i = 0; i < length; i++) {
    char c = text[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > 8) return nullptr; }
    else if (c == '}' || c == ']') { if (!depth) return nullptr; --depth; }
  }
  if (quoted || depth) return nullptr;
  return cJSON_ParseWithLengthOpts(text, length + 1, nullptr, true);
}

// Parse only bounded, exact fields; construct the download URL from our trusted
// origin and signed digest, never from an unauthenticated response URL.
inline bool parseSignedUpdate(const cJSON *doc, UpdateJob &job) {
  auto v = cJSON_GetObjectItemCaseSensitive(doc, "signatureVersion"), b = cJSON_GetObjectItemCaseSensitive(doc, "build"),
    c = cJSON_GetObjectItemCaseSensitive(doc, "channel"), n = cJSON_GetObjectItemCaseSensitive(doc, "size"),
    chip = cJSON_GetObjectItemCaseSensitive(doc, "chip"), target = cJSON_GetObjectItemCaseSensitive(doc, "target"),
    sha = cJSON_GetObjectItemCaseSensitive(doc, "sha256"), key = cJSON_GetObjectItemCaseSensitive(doc, "keyId"),
    signature = cJSON_GetObjectItemCaseSensitive(doc, "signature");
  if (!cJSON_IsObject(doc) || !cJSON_IsNumber(v) || v->valuedouble != 1 ||
      !cJSON_IsNumber(b) || b->valuedouble != b->valueint || b->valueint <= 0 ||
      !cJSON_IsNumber(c) || c->valuedouble != c->valueint || c->valueint < 0 || c->valueint > 100 ||
      !cJSON_IsNumber(n) || n->valuedouble != n->valueint || n->valueint < 288 || n->valueint > LUXIO_APP_SIZE ||
      !cJSON_IsString(chip) || strcmp(chip->valuestring, CONFIG_IDF_TARGET) ||
      !cJSON_IsString(target) || strcmp(target->valuestring, LUXIO_UPDATE_TARGET) ||
      !cJSON_IsString(sha) || strlen(sha->valuestring) != 64 ||
      !cJSON_IsString(key) || strcmp(key->valuestring, LUXIO_OTA_KEY_ID) ||
      !cJSON_IsString(signature) || strlen(signature->valuestring) < 92 || strlen(signature->valuestring) >= sizeof(job.signature)) return false;
  job = {};
  job.build = b->valueint; job.channel = c->valueint; job.size = n->valueint;
  strcpy(job.target, target->valuestring); strcpy(job.sha256, sha->valuestring);
  strcpy(job.keyId, key->valuestring); strcpy(job.signature, signature->valuestring);
  snprintf(job.url, sizeof(job.url), "%s/firmware/%s", LUXIO_CLOUD_URL, job.sha256);
  return validUpdateJob(job);
}
