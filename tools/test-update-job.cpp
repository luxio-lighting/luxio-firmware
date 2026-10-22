#include "../Luxio/src/update_job.h"
#include <cassert>
int main() {
  UpdateJob valid; valid.build = 7; valid.size = 2258000;
  strcpy(valid.url, "https://firmware.example/luxio.bin");
  memset(valid.sha256, 'a', 64); strcpy(valid.target, LUXIO_UPDATE_TARGET);
  assert(validUpdateJob(valid));
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
