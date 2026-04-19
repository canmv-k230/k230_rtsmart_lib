/* PUF Secure Engine CCM ALT for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MBEDTLS_CCM_ALT_H
#define MBEDTLS_CCM_ALT_H

#include <stdint.h>
#include "drv_pufs.h"

typedef struct mbedtls_ccm_context {
    uint8_t key[32];
    unsigned int keybits;
    int has_key;
    /* Streaming state */
    drv_pufs_cipher_inst cipher;
    int mode;       /* MBEDTLS_CCM_ENCRYPT or MBEDTLS_CCM_DECRYPT */
    int started;    /* cipher_ccm_init has been called */
    /* Deferred init: need both starts() and set_lengths() before HW init */
    unsigned char iv[16];
    size_t iv_len;
    size_t total_ad_len;
    size_t plaintext_len;
    size_t tag_len;
    int have_iv;
    int have_lengths;
} mbedtls_ccm_context;

#endif /* MBEDTLS_CCM_ALT_H */
