#pragma once
#include <cstdint>
#include <cstring>

// Persisted as one NVS blob: a torn write cannot mix fields from two updates.
// Recovery is fixed at 0x10000; only the application at 0x130000 is replaceable.
constexpr uint32_t LUXIO_APP_SIZE = 0x280000;
constexpr char LUXIO_UPDATE_TARGET[] = "esp32s3-recovery-v1";
struct UpdateJob {
  uint32_t version = 1;
  uint32_t build = 0;
  uint32_t size = 0;
  char url[1025] = {};
  char sha256[65] = {};
  char target[32] = {};
};
inline bool validUpdateJob(const UpdateJob &job) {
  if (job.version != 1 || !job.build || job.build > 2147483647 || job.size < 288 || job.size > LUXIO_APP_SIZE) return false;
  if (strnlen(job.target, sizeof(job.target)) != strlen(LUXIO_UPDATE_TARGET) || strcmp(job.target, LUXIO_UPDATE_TARGET)) return false;
  size_t length = strnlen(job.url, sizeof(job.url));
  if (length < 9 || length == sizeof(job.url) || strncmp(job.url, "https://", 8)) return false;
  for (size_t i = 0; i < length; i++) if (static_cast<unsigned char>(job.url[i]) <= 32) return false;
  if (strnlen(job.sha256, sizeof(job.sha256)) != 64) return false;
  for (int i = 0; i < 64; i++) if (!((job.sha256[i] >= '0' && job.sha256[i] <= '9') || (job.sha256[i] >= 'a' && job.sha256[i] <= 'f'))) return false;
  return true;
}
