#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>
#define LUXIO_STRING_VALUE_INNER(value) #value
#define LUXIO_STRING_VALUE(value) LUXIO_STRING_VALUE_INNER(value)
#ifndef LUXIO_BUILD
#define LUXIO_BUILD 36
#endif
// Override the SDK's weak descriptor so main can require a signature-verifying recovery build.
extern "C" const esp_app_desc_t esp_app_desc __attribute__((section(".rodata_desc"), used)) = {
  .magic_word = ESP_APP_DESC_MAGIC_WORD,
  .secure_version = 0,
  .reserv1 = {},
  .version = LUXIO_STRING_VALUE(LUXIO_BUILD),
  .project_name = "Recovery",
  .time = __TIME__,
  .date = __DATE__,
  .idf_ver = IDF_VER,
  .app_elf_sha256 = {},
  .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
  .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
  .mmu_page_size = 16,
};
