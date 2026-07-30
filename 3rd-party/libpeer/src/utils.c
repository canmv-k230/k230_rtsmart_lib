#include "utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mbedtls/md.h"

void utils_random_string(char* s, const int len) {
  int i;

  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";

  srand(time(NULL));

  for (i = 0; i < len; ++i) {
    s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  s[len] = '\0';
}

int utils_get_hmac_sha1(const char* input, size_t input_len, const char* key, size_t key_len, unsigned char* output) {
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  int ret;

  if (!md_info) {
    return -1;
  }

  mbedtls_md_init(&ctx);
  ret = mbedtls_md_setup(&ctx, md_info, 1);
  if (ret == 0) {
    ret = mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key, key_len);
  }
  if (ret == 0) {
    ret = mbedtls_md_hmac_update(&ctx, (const unsigned char*)input, input_len);
  }
  if (ret == 0) {
    ret = mbedtls_md_hmac_finish(&ctx, output);
  }
  mbedtls_md_free(&ctx);
  return ret;
}

void utils_get_md5(const char* input, size_t input_len, unsigned char* output) {
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_MD5;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)input, input_len);
  mbedtls_md_finish(&ctx, output);
  mbedtls_md_free(&ctx);
}
