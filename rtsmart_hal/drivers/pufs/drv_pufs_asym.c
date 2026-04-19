/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/ioctl.h>
#include <string.h>
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

/* ===== UID ===== */

int drv_pufs_uid_get(drv_pufs_inst *dev, uint8_t slot, uint8_t uid[32])
{
    pufs_uid_get_t arg;
    int ret;

    if (!dev || dev->fd < 0 || !uid)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.slot = slot;
    arg.uid_phys = (uint64_t)(uintptr_t)uid;

    ret = drv_pufs_ioctl(dev->fd, PUFS_UID_GET, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== ECDSA ===== */

int drv_pufs_ecdsa_sign(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                        uint8_t prktype, uint8_t prkslot,
                        const uint8_t *md, uint32_t mdlen,
                        pufs_ecdsa_sig_t *sig)
{
    int ret;

    if (!dev || dev->fd < 0 || !md || !sig)
        return -1;

    if (mdlen > dev->buf_size)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_ecdsa_sign_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.ecctype = ecctype;
    arg.prktype = prktype;
    arg.prkslot = prkslot;
    arg.mdlen = mdlen;

    arg.md_phys = (uint64_t)(uintptr_t)md;
    arg.sig_phys = (uint64_t)(uintptr_t)sig;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ECDSA_SIGN, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_ecdsa_verify(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                          const uint8_t *md, uint32_t mdlen,
                          const pufs_ecc_puk_t *puk,
                          const pufs_ecdsa_sig_t *sig)
{
    uint32_t input_offset = 0;
    int ret;

    if (!dev || dev->fd < 0 || !md || !puk || !sig)
        return -1;

    if (mdlen > dev->buf_size)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_ecdsa_verify_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.ecctype = ecctype;
    arg.mdlen = mdlen;

    (void)input_offset;
    arg.md_phys = (uint64_t)(uintptr_t)md;
    arg.sig_phys = (uint64_t)(uintptr_t)sig;
    arg.puk_phys = (uint64_t)(uintptr_t)puk;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ECDSA_VERIFY, &arg);
    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== RSA ===== */

int drv_pufs_rsa_sign(drv_pufs_inst *dev, pufs_rsamode_t mode,
                      pufs_rsatype_t rsatype, pufs_hashtype_t hashtype,
                      uint32_t puk, const uint8_t *n, const uint8_t *prk,
                      const uint8_t *msg, uint32_t msglen,
                      const uint8_t *salt, uint32_t saltlen,
                      uint8_t *sig)
{
    uint32_t elen;
    uint32_t msg_input_len;
    int ret;

    if (!dev || dev->fd < 0 || !sig)
        return -1;

    elen = (rsatype + 1U) * 128U;
    msg_input_len = (mode == RSA_BASE) ? elen : msglen;
    if ((uint64_t)(elen * 2) + msg_input_len + saltlen > dev->buf_size || elen > dev->buf_size)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_rsa_sign_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.rsamode = mode;
    arg.rsatype = rsatype;
    arg.hashtype = hashtype;
    arg.puk = puk;
    arg.msglen = msglen;
    arg.saltlen = saltlen;

    if (!n || !prk) {
        drv_pufs_dev_unlock(dev);
        return -1;
    }

    arg.n_phys = (uint64_t)(uintptr_t)n;
    arg.prk_phys = (uint64_t)(uintptr_t)prk;
    arg.msg_phys = (uint64_t)(uintptr_t)msg;
    arg.salt_phys = (uint64_t)(uintptr_t)salt;
    arg.sig_phys = (uint64_t)(uintptr_t)sig;

    ret = drv_pufs_ioctl(dev->fd, PUFS_RSA_SIGN, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_rsa_verify(drv_pufs_inst *dev, pufs_rsamode_t mode,
                        pufs_rsatype_t rsatype, pufs_hashtype_t hashtype,
                        uint32_t puk, const uint8_t *n,
                        const uint8_t *msg, uint32_t msglen,
                        const uint8_t *sig)
{
    uint32_t elen;
    uint32_t msg_input_len;
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    elen = (rsatype + 1U) * 128U;
    msg_input_len = (mode == RSA_BASE) ? elen : msglen;
    if ((uint64_t)(elen * 2) + msg_input_len > dev->buf_size)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_rsa_verify_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.rsamode = mode;
    arg.rsatype = rsatype;
    arg.hashtype = hashtype;
    arg.puk = puk;
    arg.msglen = msglen;

    if (!n || !sig)
        goto fail;

    arg.n_phys = (uint64_t)(uintptr_t)n;
    arg.msg_phys = (uint64_t)(uintptr_t)msg;
    arg.sig_phys = (uint64_t)(uintptr_t)sig;

    ret = drv_pufs_ioctl(dev->fd, PUFS_RSA_VERIFY, &arg);
    drv_pufs_dev_unlock(dev);
    return ret;

fail:
    drv_pufs_dev_unlock(dev);
    return -1;
}

/* ===== SM2 ===== */

int drv_pufs_sm2_sign(drv_pufs_inst *dev, uint8_t prktype, uint8_t prkslot,
                      const uint8_t *id, uint32_t idlen,
                      const uint8_t *msg, uint32_t msglen,
                      pufs_ecdsa_sig_t *sig)
{
    int ret;

    if (!dev || dev->fd < 0 || !sig)
        return -1;

    if ((uint64_t)idlen + msglen > dev->buf_size)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sm2_sign_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.prktype = prktype;
    arg.prkslot = prkslot;
    arg.idlen = idlen;
    arg.msglen = msglen;

    arg.id_phys = (uint64_t)(uintptr_t)id;
    arg.msg_phys = (uint64_t)(uintptr_t)msg;
    arg.sig_phys = (uint64_t)(uintptr_t)sig;

    ret = drv_pufs_ioctl(dev->fd, PUFS_SM2_SIGN, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_sm2_verify(drv_pufs_inst *dev,
                        const pufs_ecc_puk_t *puk,
                        const uint8_t *id, uint32_t idlen,
                        const uint8_t *msg, uint32_t msglen,
                        const pufs_ecdsa_sig_t *sig)
{
    uint32_t input_offset = 0;
    int ret;

    if (!dev || dev->fd < 0 || !puk || !sig)
        return -1;

    if ((uint64_t)idlen + msglen > dev->buf_size)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sm2_verify_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.idlen = idlen;
    arg.msglen = msglen;

    (void)input_offset;
    arg.puk_phys = (uint64_t)(uintptr_t)puk;
    arg.id_phys = (uint64_t)(uintptr_t)id;
    arg.msg_phys = (uint64_t)(uintptr_t)msg;
    arg.sig_phys = (uint64_t)(uintptr_t)sig;

    ret = drv_pufs_ioctl(dev->fd, PUFS_SM2_VERIFY, &arg);
    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== ECC key management ===== */

int drv_pufs_ecc_prk_gen(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                         int is_ephemeral, uint8_t prkslot)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_ecc_prk_gen_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.ecctype = ecctype;
    arg.is_ephemeral = is_ephemeral ? 1 : 0;
    arg.prkslot = prkslot;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ECC_PRK_GEN, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_ecc_puk_gen(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                         uint8_t prktype, uint8_t prkslot,
                         pufs_ecc_puk_t *puk)
{
    int ret;

    if (!dev || dev->fd < 0 || !puk)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_ecc_puk_gen_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.ecctype = ecctype;
    arg.prktype = prktype;
    arg.prkslot = prkslot;
    arg.puk = puk;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ECC_PUK_GEN, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_ecc_puk_verify(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                            const pufs_ecc_puk_t *puk)
{
    int ret;

    if (!dev || dev->fd < 0 || !puk)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_ecc_puk_verify_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.ecctype = ecctype;
    arg.puk = (pufs_ecc_puk_t *)puk;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ECC_PUK_VERIFY, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_ecc_cdh(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                     int is_ephemeral, uint8_t prkslot_e,
                     const pufs_ecc_puk_t *puk_e,
                     uint8_t *out, uint32_t outlen)
{
    int ret;

    if (!dev || dev->fd < 0 || !puk_e || !out)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_ecc_cdh_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.ecctype = ecctype;
    arg.is_ephemeral = is_ephemeral ? 1 : 0;
    arg.prkslot_e = prkslot_e;
    arg.puk_e = (pufs_ecc_puk_t *)puk_e;
    arg.out = out;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ECC_CDH, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== SM2 encryption / key exchange ===== */

int drv_pufs_sm2_enc(drv_pufs_inst *dev, pufs_sm2ct_format_t format,
                     const uint8_t *in, uint32_t inlen,
                     uint8_t *out, uint32_t *outlen,
                     const pufs_ecc_puk_t *puk)
{
    int ret;

    if (!dev || dev->fd < 0 || !in || !out || !outlen || !puk)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sm2_enc_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.format = format;
    arg.inlen = inlen;
    arg.in = (uint8_t *)in;
    arg.out = out;
    arg.outlen = outlen;
    arg.puk = (pufs_ecc_puk_t *)puk;

    ret = drv_pufs_ioctl(dev->fd, PUFS_SM2_ENC, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_sm2_dec(drv_pufs_inst *dev, pufs_sm2ct_format_t format,
                     uint8_t prkslot,
                     const uint8_t *in, uint32_t inlen,
                     uint8_t *out, uint32_t *outlen)
{
    int ret;

    if (!dev || dev->fd < 0 || !in || !out || !outlen)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sm2_dec_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.format = format;
    arg.prkslot = prkslot;
    arg.inlen = inlen;
    arg.in = (uint8_t *)in;
    arg.out = out;
    arg.outlen = outlen;

    ret = drv_pufs_ioctl(dev->fd, PUFS_SM2_DEC, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_sm2_kex(drv_pufs_inst *dev, int init,
                     uint8_t prkslotl, uint8_t tprkslotl,
                     const pufs_ecc_puk_t *pukr, const pufs_ecc_puk_t *tpukr,
                     const uint8_t *idl, uint32_t idllen,
                     const uint8_t *idr, uint32_t idrlen,
                     uint8_t *key, uint32_t keybits,
                     uint8_t *dgst2, uint32_t *dlen2,
                     uint8_t *dgst3, uint32_t *dlen3)
{
    int ret;

    if (!dev || dev->fd < 0 || !pukr || !tpukr || !key)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sm2_kex_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.init = init ? 1 : 0;
    arg.prkslotl = prkslotl;
    arg.tprkslotl = tprkslotl;
    arg.idllen = idllen;
    arg.idrlen = idrlen;
    arg.keybits = keybits;
    arg.pukr = (pufs_ecc_puk_t *)pukr;
    arg.tpukr = (pufs_ecc_puk_t *)tpukr;
    arg.idl = (uint8_t *)idl;
    arg.idr = (uint8_t *)idr;
    arg.key = key;
    arg.dgst2 = dgst2;
    arg.dlen2 = dlen2;
    arg.dgst3 = dgst3;
    arg.dlen3 = dlen3;

    ret = drv_pufs_ioctl(dev->fd, PUFS_SM2_KEX, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== Key management ===== */

int drv_pufs_key_import_plaintext(drv_pufs_inst *dev,
                                  pufs_keytype_t keytype, uint8_t keyslot,
                                  const uint8_t *key, uint32_t keybits)
{
    int ret;

    if (!dev || dev->fd < 0 || !key)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_key_io_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.mode = KM_IMPORT_PT;
    arg.keytype = keytype;
    arg.keyslot = keyslot;
    arg.keyaddr = (uint8_t *)key;
    arg.keybits = keybits;

    ret = drv_pufs_ioctl(dev->fd, PUFS_KEY_INOUT, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_key_clear(drv_pufs_inst *dev,
                       pufs_keytype_t keytype, uint8_t keyslot,
                       uint32_t keybits)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_key_io_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.mode = KM_CLEAR;
    arg.keytype = keytype;
    arg.keyslot = keyslot;
    arg.keybits = keybits;

    ret = drv_pufs_ioctl(dev->fd, PUFS_KEY_INOUT, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== Key derivation ===== */

int drv_pufs_key_derive(drv_pufs_inst *dev,
                        pufs_keytype_t keytype, uint8_t keyslot,
                        pufs_kd_md_t method, pufs_kd_prf_t prf,
                        pufs_hashtype_t hash, int feedback,
                        pufs_keytype_t ztype, const uint8_t *zaddr, uint32_t zbits,
                        const uint8_t *salt, uint32_t saltlen,
                        const uint8_t *info, uint32_t infolen,
                        const uint8_t *iv, uint32_t outbits,
                        uint32_t iter, uint32_t ctrpos, uint32_t ctrlen,
                        uint8_t *out)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_key_derive_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.keytype = keytype;
    arg.keyslot = keyslot;
    arg.method = method;
    arg.prf = prf;
    arg.hash = hash;
    arg.feedback = feedback;
    arg.ztype = ztype;
    arg.zbits = zbits;
    arg.outbits = outbits;
    arg.iter = iter;
    arg.ctrpos = ctrpos;
    arg.ctrlen = ctrlen;
    arg.saltlen = saltlen;
    arg.infolen = infolen;
    arg.iv = (uint8_t *)iv;
    arg.zaddr = (uint8_t *)zaddr;
    arg.salt = (uint8_t *)salt;
    arg.info = (uint8_t *)info;
    arg.out = out;

    ret = drv_pufs_ioctl(dev->fd, PUFS_KEY_DERIVE, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== OTP ===== */

int drv_pufs_otp_read(drv_pufs_inst *dev, uint16_t addr, uint8_t *buf, uint32_t len)
{
    int ret;

    if (!dev || dev->fd < 0 || !buf || len == 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_otp_rw_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.addr = addr;
    arg.len = len;
    arg.buf = buf;

    ret = drv_pufs_ioctl(dev->fd, PUFS_OTP_READ, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_otp_write(drv_pufs_inst *dev, uint16_t addr, const uint8_t *buf, uint32_t len)
{
    int ret;

    if (!dev || dev->fd < 0 || !buf || len == 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_otp_rw_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.addr = addr;
    arg.len = len;
    arg.buf = (uint8_t *)buf;

    ret = drv_pufs_ioctl(dev->fd, PUFS_OTP_WRITE, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_otp_lock(drv_pufs_inst *dev, uint16_t addr, uint32_t len, uint8_t lock)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_otp_lock_op_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.addr = addr;
    arg.len = len;
    arg.lock = lock;

    ret = drv_pufs_ioctl(dev->fd, PUFS_OTP_LOCK, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== RNG ===== */

int drv_pufs_rng_read(drv_pufs_inst *dev, uint8_t *buf, uint32_t len)
{
    int ret;

    if (!dev || dev->fd < 0 || !buf || len == 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_rng_read_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.len = len;
    arg.buf = buf;

    ret = drv_pufs_ioctl(dev->fd, PUFS_RNG_READ, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== OTP rwlck query ===== */

int drv_pufs_otp_get_rwlck(drv_pufs_inst *dev, uint16_t addr, uint8_t *lock)
{
    int ret;

    if (!dev || dev->fd < 0 || !lock)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_otp_rwlck_get_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.addr = addr;

    ret = drv_pufs_ioctl(dev->fd, PUFS_OTP_RWLCK_GET, &arg);
    if (ret == 0)
        *lock = arg.lock;

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== Key to OTP ===== */

int drv_pufs_key_to_otp(drv_pufs_inst *dev, pufs_rt_slot_t slot,
                        const uint8_t *key, uint32_t keybits,
                        uint8_t lock)
{
    int ret;

    if (!dev || dev->fd < 0 || !key)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_key2otp_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.slot = slot;
    arg.keybits = keybits;
    arg.key = (uint8_t *)key;
    arg.lock = lock;

    ret = drv_pufs_ioctl(dev->fd, PUFS_KEY2OTP, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== PUFrt management ===== */

int drv_pufs_rt_version(drv_pufs_inst *dev, uint32_t *version, uint32_t *features)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_rt_version_t arg;
    ret = drv_pufs_ioctl(dev->fd, PUFS_RT_VERSION, &arg);
    if (ret == 0) {
        if (version) *version = arg.version;
        if (features) *features = arg.features;
    }

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_zeroize(drv_pufs_inst *dev, pufs_rt_slot_t slot)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_zeroize_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.slot = slot;

    ret = drv_pufs_ioctl(dev->fd, PUFS_ZEROIZE, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_post_mask(drv_pufs_inst *dev, uint64_t maskslots)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_post_mask_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.maskslots = maskslots;

    ret = drv_pufs_ioctl(dev->fd, PUFS_POST_MASK, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== Key export ===== */

int drv_pufs_key_export_plaintext(drv_pufs_inst *dev,
                                  pufs_keytype_t keytype, uint8_t keyslot,
                                  uint8_t *key, uint32_t keybits)
{
    int ret;

    if (!dev || dev->fd < 0 || !key)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_key_io_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.mode = KM_EXPORT_PT;
    arg.keytype = keytype;
    arg.keyslot = keyslot;
    arg.keyaddr = key;
    arg.keybits = keybits;

    ret = drv_pufs_ioctl(dev->fd, PUFS_KEY_INOUT, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== DRBG (SP800-90A) ===== */

int drv_pufs_drbg_instantiate(drv_pufs_inst *dev, pufs_drbg_type_t mode,
                              uint32_t security, int df,
                              const uint8_t *nonce, uint32_t noncelen,
                              const uint8_t *pstr, uint32_t pstrlen)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_drbg_init_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.mode = mode;
    arg.security = security;
    arg.df = df;
    arg.noncelen = noncelen;
    arg.pstrlen = pstrlen;
    arg.nonce = (uint8_t *)nonce;
    arg.pstr = (uint8_t *)pstr;

    ret = drv_pufs_ioctl(dev->fd, PUFS_DRBG_INIT, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_drbg_reseed(drv_pufs_inst *dev, int df,
                         const uint8_t *adin, uint32_t adinlen)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_drbg_reseed_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.df = df;
    arg.adinlen = adinlen;
    arg.adin = (uint8_t *)adin;

    ret = drv_pufs_ioctl(dev->fd, PUFS_DRBG_RESEED, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_drbg_generate(drv_pufs_inst *dev, uint8_t *out, uint32_t outbits,
                           int pr, int df,
                           const uint8_t *adin, uint32_t adinlen)
{
    int ret;

    if (!dev || dev->fd < 0 || !out)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_drbg_generate_t arg;
    hal_rvv_memset(&arg, 0, sizeof(arg));
    arg.outbits = outbits;
    arg.pr = pr;
    arg.df = df;
    arg.adinlen = adinlen;
    arg.adin = (uint8_t *)adin;
    arg.out = out;

    ret = drv_pufs_ioctl(dev->fd, PUFS_DRBG_GENERATE, &arg);

    drv_pufs_dev_unlock(dev);
    return ret;
}

int drv_pufs_drbg_uninstantiate(drv_pufs_inst *dev)
{
    int ret;

    if (!dev || dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    ret = drv_pufs_ioctl(dev->fd, PUFS_DRBG_UNINIT, NULL);

    drv_pufs_dev_unlock(dev);
    return ret;
}
