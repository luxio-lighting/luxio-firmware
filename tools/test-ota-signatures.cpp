#define LUXIO_APP_CAPACITY 0x280000
#define LUXIO_APP_OFFSET 0x130000
#define LUXIO_LOADER_CAPACITY 0x120000
#include "signed_update.h"
#include "vector.h"
#include <cassert>
int main() {
  UpdateJob job;
  job.build = 32; job.channel = 0; job.size = 123456;
  strcpy(job.keyId, LUXIO_OTA_KEY_ID); strcpy(job.target, LUXIO_UPDATE_TARGET);
  memset(job.sha256, 'a', 64);
  strcpy(job.signature, TEST_SIGNATURE);
  snprintf(job.url, sizeof(job.url), "%s/firmware/%s", LUXIO_CLOUD_URL, job.sha256);
  assert(verifySignedUpdate(job, 31, 0));
  assert(!verifySignedUpdate(job, 32));
  assert(!verifySignedUpdate(job, 33));
  auto bad = job; bad.build++; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.channel = 100; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.size++; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.sha256[0] = 'b'; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.keyId[0] = 'x'; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.signature[0] = 'X'; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.signature[0] = 0; assert(!verifySignedUpdate(bad, 31));
  bad = job; bad.version = 1; assert(!verifySignedUpdate(bad, 31));
  bad = job; strcpy(bad.target, "ab-v1"); assert(!verifySignedUpdate(bad, 31));
  bad = job; strcpy(bad.url, "http://127.0.0.1/private"); assert(!verifySignedUpdate(bad, 31));
  bad = job; memset(bad.signature, 'a', sizeof(bad.signature)); assert(!verifySignedUpdate(bad, 31));
  bad = job; strcpy(bad.signature, WRONG_KEY_SIGNATURE); assert(!verifySignedUpdate(bad, 31));
  bad = job; strcpy(bad.signature, OTHER_CHIP_SIGNATURE); assert(!verifySignedUpdate(bad, 31));
  bad = job; strcpy(bad.target, OTHER_LAYOUT); strcpy(bad.signature, OTHER_LAYOUT_SIGNATURE); assert(!verifySignedUpdate(bad, 31));
  puts("PASS: production MbedTLS verifier accepts Node ECDSA and rejects tampering, wrong keys/chips/layouts, URL changes and rollback");
}
