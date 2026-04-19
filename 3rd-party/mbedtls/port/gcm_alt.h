/* PUF Secure Engine GCM ALT for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MBEDTLS_GCM_ALT_H
#define MBEDTLS_GCM_ALT_H

#include <stdint.h>
#include "drv_pufs.h"

typedef struct mbedtls_gcm_context {
    uint8_t key[32];
    unsigned int keybits;
    int has_key;
    /* Streaming state */
    drv_pufs_cipher_inst cipher;
    int mode;       /* MBEDTLS_GCM_ENCRYPT or MBEDTLS_GCM_DECRYPT */
    int started;
} mbedtls_gcm_context;

#endif /* MBEDTLS_GCM_ALT_H */
