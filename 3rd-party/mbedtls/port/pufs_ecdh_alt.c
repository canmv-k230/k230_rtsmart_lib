/* PUF Secure Engine ECDH ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/ecdh.h"

#if defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT) || defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT)

#include <string.h>
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "drv_pufs.h"
#include "hal_rvv_ops.h"
#include <pthread.h>

/* ---- shared device handle ---- */
static drv_pufs_inst s_ecdh_dev;
static int s_ecdh_dev_ready = 0;
static pthread_once_t s_ecdh_once = PTHREAD_ONCE_INIT;

static void ecdh_dev_init_once(void)
{
    if (drv_pufs_open(&s_ecdh_dev) == 0)
        s_ecdh_dev_ready = 1;
}

static drv_pufs_inst *ecdh_get_dev(void)
{
    pthread_once(&s_ecdh_once, ecdh_dev_init_once);
    return s_ecdh_dev_ready ? &s_ecdh_dev : NULL;
}

/* ---- curve mapping ---- */

typedef struct {
    mbedtls_ecp_group_id mbedtls_id;
    pufs_ecctype_t       pufs_id;
    uint32_t             qlen;
} ecdh_curve_entry;

static const ecdh_curve_entry ecdh_curve_map[] = {
    { MBEDTLS_ECP_DP_SECP192R1, ECC_NISTP192, 24 },
    { MBEDTLS_ECP_DP_SECP224R1, ECC_NISTP224, 28 },
    { MBEDTLS_ECP_DP_SECP256R1, ECC_NISTP256, 32 },
    { MBEDTLS_ECP_DP_SECP384R1, ECC_NISTP384, 48 },
    { MBEDTLS_ECP_DP_SECP521R1, ECC_NISTP521, 66 },
};

static const ecdh_curve_entry *ecdh_find_curve(mbedtls_ecp_group_id id)
{
    for (size_t i = 0; i < sizeof(ecdh_curve_map) / sizeof(ecdh_curve_map[0]); i++) {
        if (ecdh_curve_map[i].mbedtls_id == id)
            return &ecdh_curve_map[i];
    }
    return NULL;
}

#endif /* MBEDTLS_ECDH_COMPUTE_SHARED_ALT || MBEDTLS_ECDH_GEN_PUBLIC_ALT */

#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT)

int mbedtls_ecdh_gen_public(mbedtls_ecp_group *grp, mbedtls_mpi *d,
                            mbedtls_ecp_point *Q,
                            int (*f_rng)(void *, unsigned char *, size_t),
                            void *p_rng)
{
    drv_pufs_inst *dev;
    const ecdh_curve_entry *cm;
    pufs_ecc_puk_t puk;
    int ret;

    if (grp == NULL || d == NULL || Q == NULL || f_rng == NULL)
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

    cm = ecdh_find_curve(grp->id);
    dev = ecdh_get_dev();

    if (cm == NULL || dev == NULL) {
        /* Software fallback: generate private key, compute Q = d * G */
        MBEDTLS_MPI_CHK(mbedtls_ecp_gen_privkey(grp, d, f_rng, p_rng));
        MBEDTLS_MPI_CHK(mbedtls_ecp_mul(grp, Q, d, &grp->G, f_rng, p_rng));
    cleanup:
        return ret;
    }

    /* Generate ephemeral private key in PRK slot */
    ret = drv_pufs_ecc_prk_gen(dev, cm->pufs_id, 1, KS_PRK_0);
    if (ret != 0)
        goto sw_fallback;

    /* Generate public key from that slot */
    hal_rvv_memset(&puk, 0, sizeof(puk));
    ret = drv_pufs_ecc_puk_gen(dev, cm->pufs_id, KT_PRKEY, KS_PRK_0, &puk);
    if (ret != 0) {
        drv_pufs_key_clear(dev, KT_PRKEY, KS_PRK_0, cm->qlen * 8);
        goto sw_fallback;
    }

    /* Convert public key to mbedtls point */
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&Q->X, puk.x, cm->qlen));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&Q->Y, puk.y, cm->qlen));
    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(&Q->Z, 1));

    /*
     * The private key stays in the HW PRK slot. We store the slot index
     * as a sentinel value in d so compute_shared can detect and use the
     * HW slot. Bit 0-7 = slot, bit 8 = sentinel flag.
     */
    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(d, 0x100 | KS_PRK_0));

    return 0;

sw_fallback:
    ret = mbedtls_ecp_gen_privkey(grp, d, f_rng, p_rng);
    if (ret != 0) return ret;
    return mbedtls_ecp_mul(grp, Q, d, &grp->G, f_rng, p_rng);
}

#endif /* MBEDTLS_ECDH_GEN_PUBLIC_ALT */

#if defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT)

int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *grp, mbedtls_mpi *z,
                                const mbedtls_ecp_point *Q,
                                const mbedtls_mpi *d,
                                int (*f_rng)(void *, unsigned char *, size_t),
                                void *p_rng)
{
    drv_pufs_inst *dev;
    const ecdh_curve_entry *cm;
    pufs_ecc_puk_t puk;
    uint8_t shared[PUFS_QLEN_MAX];
    uint8_t keybuf[PUFS_QLEN_MAX];
    int ret;

    if (grp == NULL || z == NULL || Q == NULL || d == NULL)
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

    cm = ecdh_find_curve(grp->id);
    dev = ecdh_get_dev();

    if (cm == NULL || dev == NULL)
        goto sw_fallback;

    /* Convert peer public key to PUF format */
    hal_rvv_memset(&puk, 0, sizeof(puk));
    puk.qlen = cm->qlen;
    ret = mbedtls_mpi_write_binary(&Q->X, puk.x, cm->qlen);
    if (ret != 0) return ret;
    ret = mbedtls_mpi_write_binary(&Q->Y, puk.y, cm->qlen);
    if (ret != 0) return ret;

    /*
     * Check if the private key is in a HW slot (sentinel from gen_public)
     * or if it's a raw software key that needs importing.
     */
    int need_clear = 0;

#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT)
    if (mbedtls_mpi_cmp_int(d, 0x100) >= 0 &&
        mbedtls_mpi_bitlen(d) <= 9)
    {
        /* Key is already in HW slot from gen_public */
        /* No import/clear needed, slot is already loaded */
    } else
#endif
    {
        /* Import software key into PRK slot.
         * KWP plaintext import only works for keys up to 256 bits;
         * skip HW for larger curves to avoid KWP errors. */
        if (cm->qlen > 32)
            goto sw_fallback;

        hal_rvv_memset(keybuf, 0, sizeof(keybuf));
        ret = mbedtls_mpi_write_binary(d, keybuf, cm->qlen);
        if (ret != 0) return ret;

        ret = drv_pufs_key_import_plaintext(dev, KT_PRKEY, KS_PRK_0,
                                            keybuf, cm->qlen * 8);
        hal_rvv_memset(keybuf, 0, sizeof(keybuf));
        if (ret != 0)
            goto sw_fallback;
        need_clear = 1;
    }

    /* Perform ECDH CDH */
    hal_rvv_memset(shared, 0, sizeof(shared));
    ret = drv_pufs_ecc_cdh(dev, cm->pufs_id, 1, KS_PRK_0,
                           &puk, shared, cm->qlen);

    if (need_clear)
        drv_pufs_key_clear(dev, KT_PRKEY, KS_PRK_0, cm->qlen * 8);

    if (ret != 0)
        goto sw_fallback;

    /* Convert shared secret to MPI */
    ret = mbedtls_mpi_read_binary(z, shared, cm->qlen);
    hal_rvv_memset(shared, 0, sizeof(shared));
    return ret;

sw_fallback:
    {
        mbedtls_ecp_point P;
        mbedtls_ecp_point_init(&P);
        MBEDTLS_MPI_CHK(mbedtls_ecp_mul(grp, &P, d, Q, f_rng, p_rng));
        if (mbedtls_ecp_is_zero(&P)) {
            ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
            goto cleanup;
        }
        MBEDTLS_MPI_CHK(mbedtls_mpi_copy(z, &P.X));
    cleanup:
        mbedtls_ecp_point_free(&P);
        return ret;
    }
}

#endif /* MBEDTLS_ECDH_COMPUTE_SHARED_ALT */
