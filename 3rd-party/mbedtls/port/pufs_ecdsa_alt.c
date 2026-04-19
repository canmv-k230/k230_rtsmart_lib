/* PUF Secure Engine ECDSA verify ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Allow access to ecp_point/ecp_group/mpi private members */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/ecdsa.h"

#if defined(MBEDTLS_ECDSA_VERIFY_ALT)

#include <string.h>
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "drv_pufs.h"
#include "hal_rvv_ops.h"
#include <pthread.h>

/* ---- shared device handle ---- */
static drv_pufs_inst s_ecdsa_dev;
static int s_ecdsa_dev_ready = 0;
static pthread_once_t s_ecdsa_once = PTHREAD_ONCE_INIT;

static void ecdsa_dev_init_once(void)
{
    if (drv_pufs_open(&s_ecdsa_dev) == 0)
        s_ecdsa_dev_ready = 1;
}

static drv_pufs_inst *ecdsa_get_dev(void)
{
    pthread_once(&s_ecdsa_once, ecdsa_dev_init_once);
    return s_ecdsa_dev_ready ? &s_ecdsa_dev : NULL;
}

/* ---- curve mapping ---- */

typedef struct {
    mbedtls_ecp_group_id mbedtls_id;
    pufs_ecctype_t       pufs_id;
    uint32_t             qlen;   /* coordinate byte length */
} curve_map_entry;

static const curve_map_entry curve_map[] = {
    { MBEDTLS_ECP_DP_SECP192R1, ECC_NISTP192, 24 },
    { MBEDTLS_ECP_DP_SECP224R1, ECC_NISTP224, 28 },
    { MBEDTLS_ECP_DP_SECP256R1, ECC_NISTP256, 32 },
    { MBEDTLS_ECP_DP_SECP384R1, ECC_NISTP384, 48 },
    { MBEDTLS_ECP_DP_SECP521R1, ECC_NISTP521, 66 },
};

static const curve_map_entry *find_curve(mbedtls_ecp_group_id id)
{
    for (size_t i = 0; i < sizeof(curve_map) / sizeof(curve_map[0]); i++) {
        if (curve_map[i].mbedtls_id == id)
            return &curve_map[i];
    }
    return NULL;
}

/* ---- software fallback (reimplements SEC1 4.1.4) ---- */

static int derive_mpi(const mbedtls_ecp_group *grp, mbedtls_mpi *x,
                      const unsigned char *buf, size_t blen)
{
    int ret;
    size_t n_size = (grp->nbits + 7) / 8;
    size_t use_size = blen > n_size ? n_size : blen;

    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(x, buf, use_size));
    if (use_size * 8 > grp->nbits)
        MBEDTLS_MPI_CHK(mbedtls_mpi_shift_r(x, use_size * 8 - grp->nbits));

    if (mbedtls_mpi_cmp_mpi(x, &grp->N) >= 0)
        MBEDTLS_MPI_CHK(mbedtls_mpi_sub_mpi(x, x, &grp->N));

cleanup:
    return ret;
}

static int ecdsa_verify_sw(mbedtls_ecp_group *grp,
                           const unsigned char *buf, size_t blen,
                           const mbedtls_ecp_point *Q,
                           const mbedtls_mpi *r,
                           const mbedtls_mpi *s)
{
    int ret;
    mbedtls_mpi e, s_inv, u1, u2;
    mbedtls_ecp_point R;

    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&s_inv);
    mbedtls_mpi_init(&u1);
    mbedtls_mpi_init(&u2);

    /* step 1: check r, s in [1, N-1] */
    if (mbedtls_mpi_cmp_int(r, 1) < 0 || mbedtls_mpi_cmp_mpi(r, &grp->N) >= 0 ||
        mbedtls_mpi_cmp_int(s, 1) < 0 || mbedtls_mpi_cmp_mpi(s, &grp->N) >= 0) {
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;
        goto cleanup;
    }

    /* step 3: e = hash -> MPI */
    MBEDTLS_MPI_CHK(derive_mpi(grp, &e, buf, blen));

    /* step 4: u1 = e * s^-1 mod N, u2 = r * s^-1 mod N */
    MBEDTLS_MPI_CHK(mbedtls_mpi_inv_mod(&s_inv, s, &grp->N));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u1, &e, &s_inv));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u1, &u1, &grp->N));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&u2, r, &s_inv));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&u2, &u2, &grp->N));

    /* step 5: R = u1*G + u2*Q */
    MBEDTLS_MPI_CHK(mbedtls_ecp_muladd(grp, &R, &u1, &grp->G, &u2, Q));

    if (mbedtls_ecp_is_zero(&R)) {
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;
        goto cleanup;
    }

    /* step 7: v = R.x mod N */
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&R.X, &R.X, &grp->N));

    /* step 8: check v == r */
    if (mbedtls_mpi_cmp_mpi(&R.X, r) != 0) {
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;
        goto cleanup;
    }

cleanup:
    mbedtls_ecp_point_free(&R);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&s_inv);
    mbedtls_mpi_free(&u1);
    mbedtls_mpi_free(&u2);
    return ret;
}

/* ---- hardware-accelerated verify ---- */

static int ecdsa_verify_hw(const curve_map_entry *cm,
                           const unsigned char *buf, size_t blen,
                           const mbedtls_ecp_point *Q,
                           const mbedtls_mpi *r,
                           const mbedtls_mpi *s)
{
    drv_pufs_inst *dev = ecdsa_get_dev();
    if (!dev)
        return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;

    pufs_ecc_puk_t puk;
    pufs_ecdsa_sig_t sig;
    int ret;

    hal_rvv_memset(&puk, 0, sizeof(puk));
    hal_rvv_memset(&sig, 0, sizeof(sig));

    puk.qlen = cm->qlen;
    sig.qlen = cm->qlen;

    /* Convert MbedTLS point (MPI) → PUF raw big-endian bytes */
    ret = mbedtls_mpi_write_binary(&Q->X, puk.x, cm->qlen);
    if (ret != 0) return ret;
    ret = mbedtls_mpi_write_binary(&Q->Y, puk.y, cm->qlen);
    if (ret != 0) return ret;

    /* Convert MbedTLS signature (MPI) → PUF raw big-endian bytes */
    ret = mbedtls_mpi_write_binary(r, sig.r, cm->qlen);
    if (ret != 0) return ret;
    ret = mbedtls_mpi_write_binary(s, sig.s, cm->qlen);
    if (ret != 0) return ret;

    /* Call PUF hardware ECDSA verify */
    ret = drv_pufs_ecdsa_verify(dev, cm->pufs_id,
                                buf, (uint32_t) blen,
                                &puk, &sig);

    if (ret == PUFS_ERR_VERFAIL)
        return MBEDTLS_ERR_ECP_VERIFY_FAILED;

    if (ret != 0)
        return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;

    return 0;
}

/* ---- public API ---- */

int mbedtls_ecdsa_verify_restartable(mbedtls_ecp_group *grp,
                                     const unsigned char *buf, size_t blen,
                                     const mbedtls_ecp_point *Q,
                                     const mbedtls_mpi *r,
                                     const mbedtls_mpi *s,
                                     mbedtls_ecdsa_restart_ctx *rs_ctx)
{
    (void) rs_ctx;   /* restartable not supported with HW accel */

    if (grp == NULL || buf == NULL || Q == NULL || r == NULL || s == NULL)
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

    if (!mbedtls_ecdsa_can_do(grp->id) || grp->N.p == NULL)
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

    /* Try hardware path for supported curves */
    const curve_map_entry *cm = find_curve(grp->id);
    if (cm != NULL) {
        int ret = ecdsa_verify_hw(cm, buf, blen, Q, r, s);
        if (ret == 0)
            return ret;
        /* For VERIFY_FAILED, fall through to SW verify to catch HW false
         * negatives (observed on P-521).  For other errors (HW unavailable),
         * also fall through. */
    }

    return ecdsa_verify_sw(grp, buf, blen, Q, r, s);
}

int mbedtls_ecdsa_verify(mbedtls_ecp_group *grp,
                         const unsigned char *buf, size_t blen,
                         const mbedtls_ecp_point *Q,
                         const mbedtls_mpi *r,
                         const mbedtls_mpi *s)
{
    return mbedtls_ecdsa_verify_restartable(grp, buf, blen, Q, r, s, NULL);
}

#endif /* MBEDTLS_ECDSA_VERIFY_ALT */

#if defined(MBEDTLS_ECDSA_SIGN_ALT)

#include <string.h>
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

#ifndef MBEDTLS_ECDSA_VERIFY_ALT
/* If verify ALT is not enabled, we need our own device handle and curve map */
#include <pthread.h>

static drv_pufs_inst s_ecdsa_dev;
static int s_ecdsa_dev_ready = 0;
static pthread_once_t s_ecdsa_once = PTHREAD_ONCE_INIT;

static void ecdsa_dev_init_once(void)
{
    if (drv_pufs_open(&s_ecdsa_dev) == 0)
        s_ecdsa_dev_ready = 1;
}

static drv_pufs_inst *ecdsa_get_dev(void)
{
    pthread_once(&s_ecdsa_once, ecdsa_dev_init_once);
    return s_ecdsa_dev_ready ? &s_ecdsa_dev : NULL;
}

typedef struct {
    mbedtls_ecp_group_id mbedtls_id;
    pufs_ecctype_t       pufs_id;
    uint32_t             qlen;
} curve_map_entry;

static const curve_map_entry curve_map[] = {
    { MBEDTLS_ECP_DP_SECP192R1, ECC_NISTP192, 24 },
    { MBEDTLS_ECP_DP_SECP224R1, ECC_NISTP224, 28 },
    { MBEDTLS_ECP_DP_SECP256R1, ECC_NISTP256, 32 },
    { MBEDTLS_ECP_DP_SECP384R1, ECC_NISTP384, 48 },
    { MBEDTLS_ECP_DP_SECP521R1, ECC_NISTP521, 66 },
};

static const curve_map_entry *find_curve(mbedtls_ecp_group_id id)
{
    for (size_t i = 0; i < sizeof(curve_map) / sizeof(curve_map[0]); i++) {
        if (curve_map[i].mbedtls_id == id)
            return &curve_map[i];
    }
    return NULL;
}
#endif /* !MBEDTLS_ECDSA_VERIFY_ALT */

/* ---- software fallback for sign ---- */

static int ecdsa_sign_sw(mbedtls_ecp_group *grp, mbedtls_mpi *r, mbedtls_mpi *s,
                         const mbedtls_mpi *d, const unsigned char *buf, size_t blen,
                         int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    int ret, key_tries, sign_tries;
    mbedtls_ecp_point R;
    mbedtls_mpi k, e, t;

    if (grp == NULL || r == NULL || s == NULL || d == NULL || buf == NULL || f_rng == NULL)
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&k);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&t);

    sign_tries = 0;
    do {
        if (sign_tries++ > 10) {
            ret = MBEDTLS_ERR_ECP_RANDOM_FAILED;
            goto cleanup;
        }

        /* Generate random k */
        key_tries = 0;
        do {
            if (key_tries++ > 30) {
                ret = MBEDTLS_ERR_ECP_RANDOM_FAILED;
                goto cleanup;
            }
            MBEDTLS_MPI_CHK(mbedtls_ecp_gen_privkey(grp, &k, f_rng, p_rng));
        } while (mbedtls_mpi_cmp_int(&k, 0) == 0);

        /* R = k * G */
        MBEDTLS_MPI_CHK(mbedtls_ecp_mul(grp, &R, &k, &grp->G, f_rng, p_rng));

        /* r = R.x mod N */
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(r, &R.X, &grp->N));
    } while (mbedtls_mpi_cmp_int(r, 0) == 0);

    /* Derive MPI from hash */
    {
        size_t n_size = (grp->nbits + 7) / 8;
        size_t use_size = blen > n_size ? n_size : blen;
        MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&e, buf, use_size));
        if (use_size * 8 > grp->nbits)
            MBEDTLS_MPI_CHK(mbedtls_mpi_shift_r(&e, use_size * 8 - grp->nbits));
    }

    /* s = (e + r * d) / k mod N */
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(s, r, d));
    MBEDTLS_MPI_CHK(mbedtls_mpi_add_mpi(&e, &e, s));
    MBEDTLS_MPI_CHK(mbedtls_mpi_inv_mod(s, &k, &grp->N));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(s, s, &e));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(s, s, &grp->N));

cleanup:
    mbedtls_ecp_point_free(&R);
    mbedtls_mpi_free(&k);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&t);
    return ret;
}

/* ---- hardware-accelerated sign ---- */

static int ecdsa_sign_hw(const curve_map_entry *cm,
                         mbedtls_mpi *r, mbedtls_mpi *s,
                         const mbedtls_mpi *d,
                         const unsigned char *buf, size_t blen)
{
    /* KWP plaintext key import only works for keys up to 256 bits.
     * Larger curves (P-384, P-521) trigger HW errors in the KWP engine. */
    if (cm->qlen > 32)
        return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;

    drv_pufs_inst *dev = ecdsa_get_dev();
    if (!dev)
        return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;

    int ret;
    uint8_t keybuf[PUFS_QLEN_MAX];
    pufs_ecdsa_sig_t sig;
    uint32_t qlen = cm->qlen;

    /* Export private key from MPI to raw big-endian bytes */
    hal_rvv_memset(keybuf, 0, sizeof(keybuf));
    ret = mbedtls_mpi_write_binary(d, keybuf, qlen);
    if (ret != 0)
        return ret;

    /* Import private key into a PRK slot */
    ret = drv_pufs_key_import_plaintext(dev, KT_PRKEY, KS_PRK_0,
                                        keybuf, qlen * 8);
    /* Wipe key material immediately */
    hal_rvv_memset(keybuf, 0, sizeof(keybuf));
    if (ret != 0)
        return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;

    /* Sign using the imported PRK slot */
    hal_rvv_memset(&sig, 0, sizeof(sig));
    sig.qlen = qlen;

    ret = drv_pufs_ecdsa_sign(dev, cm->pufs_id,
                              KT_PRKEY, KS_PRK_0,
                              buf, (uint32_t)blen, &sig);

    /* Always clear the PRK slot */
    drv_pufs_key_clear(dev, KT_PRKEY, KS_PRK_0, qlen * 8);

    if (ret != 0)
        return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;

    /* Convert PUF signature → MPI */
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(r, sig.r, qlen));
    MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(s, sig.s, qlen));

cleanup:
    return ret;
}

/* ---- public API ---- */

int mbedtls_ecdsa_sign(mbedtls_ecp_group *grp, mbedtls_mpi *r, mbedtls_mpi *s,
                       const mbedtls_mpi *d, const unsigned char *buf, size_t blen,
                       int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    if (grp == NULL || r == NULL || s == NULL || d == NULL || buf == NULL)
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;

    const curve_map_entry *cm = find_curve(grp->id);
    if (cm != NULL) {
        int ret = ecdsa_sign_hw(cm, r, s, d, buf, blen);
        if (ret == 0)
            return 0;
    }

    /* Fallback to software for unsupported curves */
    return ecdsa_sign_sw(grp, r, s, d, buf, blen, f_rng, p_rng);
}

#endif /* MBEDTLS_ECDSA_SIGN_ALT */
