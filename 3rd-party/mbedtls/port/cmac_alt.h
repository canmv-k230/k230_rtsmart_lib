/* PUF Secure Engine CMAC ALT header for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MBEDTLS_CMAC_ALT_H
#define MBEDTLS_CMAC_ALT_H

#include <stdint.h>
#include "drv_pufs.h"

/**
 * Replacement CMAC context that wraps the PUF hardware CMAC engine.
 * Allocated inside mbedtls_cipher_context_t::cmac_ctx by cmac_starts().
 */
struct mbedtls_cmac_context_t {
    drv_pufs_cmac_inst cmac;      /* PUF CMAC init/update/final instance */
    uint8_t  key[32];             /* stored key (AES-128/192/256)        */
    uint32_t keybits;             /* key length in bits                  */
    int      hw_ready;            /* 1 after successful drv_pufs_cmac_init */
    int      use_sw;              /* fallback to software CMAC path      */
    unsigned char state[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    unsigned char unprocessed_block[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    size_t   unprocessed_len;
};

#endif /* MBEDTLS_CMAC_ALT_H */
