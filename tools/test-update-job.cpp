#ifndef LUXIO_APP_CAPACITY
#define LUXIO_APP_CAPACITY 0x280000
#define LUXIO_APP_OFFSET 0x130000
#define LUXIO_LOADER_CAPACITY 0x120000
#endif
#ifndef CONFIG_IDF_TARGET
#define CONFIG_IDF_TARGET "esp32s3"
#endif
#include "../Luxio/src/update_job.h"
#include <cassert>
#include <initializer_list>
int main() {
  UpdateJob valid; valid.build = 7; valid.size = 2258000;
  strcpy(valid.url, "https://firmware.example/luxio.bin");
  memset(valid.sha256, 'a', 64); strcpy(valid.target, LUXIO_UPDATE_TARGET);
  assert(validUpdateJob(valid));
  for (const char *target : {"esp32-recovery-v1", "esp32s3-recovery-v1", "esp32c3-recovery-v1", "esp32c5-recovery-v1", "esp32c6-recovery-v1"}) {
    auto other = valid; strcpy(other.target, target);
    assert(validUpdateJob(other) == (strcmp(target, LUXIO_UPDATE_TARGET) == 0));
  }
  auto bad = valid; bad.size = LUXIO_APP_SIZE+1; assert(!validUpdateJob(bad));
  bad = valid; bad.size = 0; assert(!validUpdateJob(bad));
  bad = valid; bad.version = 2; assert(!validUpdateJob(bad));
  bad = valid; bad.build = 0xffffffff; assert(!validUpdateJob(bad));
  bad = valid; strcpy(bad.url, "http://firmware.example/luxio.bin"); assert(!validUpdateJob(bad));
  bad = valid; bad.url[12] = '\n'; assert(!validUpdateJob(bad));
  bad = valid; memset(bad.url, 'a', sizeof(bad.url)); assert(!validUpdateJob(bad));
  bad = valid; strcpy(bad.target, "ab-v1"); assert(!validUpdateJob(bad));
  bad = valid; memset(bad.target, 'a', sizeof(bad.target)); assert(!validUpdateJob(bad));
  bad = valid; bad.sha256[2] = 'g'; assert(!validUpdateJob(bad));
  bad = valid; bad.sha256[64] = 'a'; assert(!validUpdateJob(bad));
}
