#pragma once
#include <cstdint>
#include <cstring>
#ifdef ARDUINO
#include <sdkconfig.h>
#endif
#ifndef CONFIG_IDF_TARGET
#error CONFIG_IDF_TARGET must identify the firmware chip.
#endif

// Persisted as one NVS blob: a torn write cannot mix fields from two updates.
// Recovery starts at 0x10000; the matrix defines its size and the main slot.
#if !defined(LUXIO_APP_CAPACITY) || !defined(LUXIO_APP_OFFSET) || !defined(LUXIO_LOADER_CAPACITY)
#error The firmware target must define its recovery layout.
#endif
constexpr uint32_t LUXIO_APP_SIZE = LUXIO_APP_CAPACITY;
constexpr uint32_t LUXIO_APP_ADDRESS = LUXIO_APP_OFFSET;
constexpr uint32_t LUXIO_LOADER_SIZE = LUXIO_LOADER_CAPACITY;
#ifndef LUXIO_UPDATE_LAYOUT
#define LUXIO_UPDATE_LAYOUT CONFIG_IDF_TARGET "-recovery-v1"
#endif
constexpr char LUXIO_UPDATE_TARGET[] = LUXIO_UPDATE_LAYOUT;
struct UpdateJob {
  uint32_t version = 2;
  uint32_t channel = 100;
  char keyId[32] = {};
  char signature[97] = {};
  uint32_t build = 0;
  uint32_t size = 0;
  char url[160] = {};
  char sha256[65] = {};
  char target[32] = {};
};
inline bool validUpdateJob(const UpdateJob &job) {
  if (job.version != 2 || job.channel > 100 || !job.build || job.build > 2147483647 || job.size < 288 || job.size > LUXIO_APP_SIZE) return false;
  if (strnlen(job.target, sizeof(job.target)) != strlen(LUXIO_UPDATE_TARGET) || strcmp(job.target, LUXIO_UPDATE_TARGET)) return false;
  size_t length = strnlen(job.url, sizeof(job.url));
  if (length < 8 || length == sizeof(job.url) || strncmp(job.url, "http://", 7)) return false;
  for (size_t i = 0; i < length; i++) if (static_cast<unsigned char>(job.url[i]) <= 32) return false;
  if (strnlen(job.sha256, sizeof(job.sha256)) != 64) return false;
  for (int i = 0; i < 64; i++) if (!((job.sha256[i] >= '0' && job.sha256[i] <= '9') || (job.sha256[i] >= 'a' && job.sha256[i] <= 'f'))) return false;
  if (strnlen(job.keyId, sizeof(job.keyId)) == 0 || strnlen(job.keyId, sizeof(job.keyId)) == sizeof(job.keyId)) return false;
  if (strnlen(job.signature, sizeof(job.signature)) < 92 || strnlen(job.signature, sizeof(job.signature)) >= sizeof(job.signature)) return false;
  return true;
}
