/* PUF Secure Engine AES ALT for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MBEDTLS_AES_ALT_H
#define MBEDTLS_AES_ALT_H

#include <stdint.h>
#include "pufs_aes_sw.h"

#define MBEDTLS_PUFS_AES_HW_THRESHOLD 4096u

typedef struct mbedtls_aes_context {
    mbedtls_aes_sw_context sw;
    unsigned char key[32];
    unsigned int keybits;
    int has_key;
} mbedtls_aes_context;

#if defined(MBEDTLS_CIPHER_MODE_XTS)
typedef struct mbedtls_aes_xts_context {
    mbedtls_aes_context crypt;
    mbedtls_aes_context tweak;
} mbedtls_aes_xts_context;
#endif

#endif /* MBEDTLS_AES_ALT_H */