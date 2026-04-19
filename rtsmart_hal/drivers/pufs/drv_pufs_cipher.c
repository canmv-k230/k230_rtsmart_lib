/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/ioctl.h>
#include <string.h>
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

#define PUFS_FINAL_MAX_OUT_LEN   (2 * PUFS_BC_BLOCK_SIZE)

/* ===== Generic cipher init (ECB/CBC/CFB/OFB/CTR/GCM) ===== */

int drv_pufs_cipher_init(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                         pufs_skcipher_t cipher, pufs_skcipher_mode_t mode,
                         int encrypt, pufs_keytype_t keytype,
                         const uint8_t *key, uint32_t keybits,
                         const uint8_t *iv, uint32_t ivlen)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    inst->mode = mode;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    if (mode >= MODE_ECB && mode <= MODE_CTR) {
        /* SP38A modes */
        pufs_sp38a_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_INIT;
        op.cipher = cipher;
        op.mode = mode;
        op.encrypt = encrypt;
        op.keytype = keytype;
        op.keybits = keybits;

        if (keytype == KT_SWKEY && key) {
            uint32_t klen = (keybits + 7) >> 3;
            if (klen > PUFS_SW_KEY_MAXLEN) klen = PUFS_SW_KEY_MAXLEN;
            hal_rvv_memcpy(op.key, key, klen);
        }
        if (iv && ivlen > 0) {
            if (ivlen > PUFS_BC_BLOCK_SIZE) ivlen = PUFS_BC_BLOCK_SIZE;
            hal_rvv_memcpy(op.iv, iv, ivlen);
            op.ivlen = ivlen;
        }

        int ret = drv_pufs_ioctl(dev->fd, PUFS_SP38A_OP, &op);
        if (ret == 0)
            hal_rvv_memcpy(&inst->ctx.sp38a, &op.ctx, sizeof(inst->ctx.sp38a));
        drv_pufs_dev_unlock(dev);
        return ret;

    } else if (mode == MODE_GCM) {
        /* SP38D GCM */
        pufs_sp38d_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_INIT;
        op.cipher = cipher;
        op.encrypt = encrypt;
        op.keytype = keytype;
        op.keybits = keybits;

        if (keytype == KT_SWKEY && key) {
            uint32_t klen = (keybits + 7) >> 3;
            if (klen > PUFS_SW_KEY_MAXLEN) klen = PUFS_SW_KEY_MAXLEN;
            hal_rvv_memcpy(op.key, key, klen);
        }
        if (iv && ivlen > 0) {
            if (ivlen > PUFS_BC_BLOCK_SIZE) ivlen = PUFS_BC_BLOCK_SIZE;
            hal_rvv_memcpy(op.iv, iv, ivlen);
            op.ivlen = ivlen;
        }

        int ret = drv_pufs_ioctl(dev->fd, PUFS_SP38D_OP, &op);
        if (ret == 0)
            hal_rvv_memcpy(&inst->ctx.sp38d, &op.ctx, sizeof(inst->ctx.sp38d));
        drv_pufs_dev_unlock(dev);
        return ret;
    }

    drv_pufs_dev_unlock(dev);
    return -1; /* CCM and XTS use their own init functions */
}

/* ===== CCM-specific init ===== */

int drv_pufs_cipher_ccm_init(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                             pufs_skcipher_t cipher, int encrypt,
                             pufs_keytype_t keytype,
                             const uint8_t *key, uint32_t keybits,
                             const uint8_t *nonce, uint32_t noncelen,
                             uint64_t aadlen, uint64_t inlen, uint32_t taglen)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    inst->mode = MODE_CCM;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sp38c_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_INIT;
    op.cipher = cipher;
    op.encrypt = encrypt;
    op.keytype = keytype;
    op.keybits = keybits;
    op.ccm_aadlen = aadlen;
    op.ccm_inlen = inlen;
    op.taglen = taglen;

    if (keytype == KT_SWKEY && key) {
        uint32_t klen = (keybits + 7) >> 3;
        if (klen > PUFS_SW_KEY_MAXLEN) klen = PUFS_SW_KEY_MAXLEN;
        hal_rvv_memcpy(op.key, key, klen);
    }
    if (nonce && noncelen > 0) {
        if (noncelen > PUFS_BC_BLOCK_SIZE) noncelen = PUFS_BC_BLOCK_SIZE;
        hal_rvv_memcpy(op.nonce, nonce, noncelen);
        op.noncelen = noncelen;
    }

    int ret = drv_pufs_ioctl(dev->fd, PUFS_SP38C_OP, &op);
    if (ret == 0)
        hal_rvv_memcpy(&inst->ctx.sp38c, &op.ctx, sizeof(inst->ctx.sp38c));
    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== XTS-specific init ===== */

int drv_pufs_cipher_xts_init(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                             pufs_skcipher_t cipher, int encrypt,
                             pufs_keytype_t keytype1, const uint8_t *key1,
                             pufs_keytype_t keytype2, const uint8_t *key2,
                             uint32_t keybits,
                             const uint8_t *iv, uint32_t ivlen)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    inst->mode = MODE_XTS;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sp38e_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_INIT;
    op.cipher = cipher;
    op.encrypt = encrypt;
    op.keytype1 = keytype1;
    op.keytype2 = keytype2;
    op.keybits = keybits;

    if (keytype1 == KT_SWKEY && key1) {
        uint32_t klen = (keybits + 7) >> 3;
        if (klen > PUFS_SW_KEY_MAXLEN) klen = PUFS_SW_KEY_MAXLEN;
        hal_rvv_memcpy(op.key1, key1, klen);
    }
    if (keytype2 == KT_SWKEY && key2) {
        uint32_t klen = (keybits + 7) >> 3;
        if (klen > PUFS_SW_KEY_MAXLEN) klen = PUFS_SW_KEY_MAXLEN;
        hal_rvv_memcpy(op.key2, key2, klen);
    }
    if (iv && ivlen > 0) {
        if (ivlen > PUFS_BC_BLOCK_SIZE) ivlen = PUFS_BC_BLOCK_SIZE;
        hal_rvv_memcpy(op.iv, iv, ivlen);
        op.ivlen = ivlen;
    }

    int ret = drv_pufs_ioctl(dev->fd, PUFS_SP38E_OP, &op);
    if (ret == 0)
        hal_rvv_memcpy(&inst->ctx.sp38e, &op.ctx, sizeof(inst->ctx.sp38e));
    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== Cipher update ===== */

int drv_pufs_cipher_update(drv_pufs_cipher_inst *inst,
                           uint8_t *out, uint32_t *outlen,
                           const uint8_t *in, uint32_t inlen)
{
    uint32_t total_out = 0;
    uint32_t in_off = 0;
    int ret = 0;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (inlen == 0) {
        if (outlen) *outlen = 0;
        return 0;
    }

    uint8_t mode = inst->mode;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    /* Dispatch once by mode — hoist op init, avoid per-chunk memset/memcpy */
    if (mode >= MODE_ECB && mode <= MODE_CTR) {
        pufs_sp38a_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_UPDATE;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38a, sizeof(op.ctx));

        while (in_off < inlen) {
            uint32_t chunk = inlen - in_off;
            if (chunk > inst->dev->buf_size)
                chunk = inst->dev->buf_size;

            op.inlen = chunk;
            op.in_phys = (uint64_t)(uintptr_t)(in + in_off);
            op.out_phys = out ? (uint64_t)(uintptr_t)(out + total_out) : 0;

            ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38A_OP, &op);
            if (ret != 0) break;

            total_out += op.outlen;
            in_off += chunk;
        }

        hal_rvv_memcpy(&inst->ctx.sp38a, &op.ctx, sizeof(inst->ctx.sp38a));
    } else if (mode == MODE_GCM) {
        pufs_sp38d_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_UPDATE;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38d, sizeof(op.ctx));

        while (in_off < inlen) {
            uint32_t chunk = inlen - in_off;
            if (chunk > inst->dev->buf_size)
                chunk = inst->dev->buf_size;

            op.inlen = chunk;
            op.in_phys = (uint64_t)(uintptr_t)(in + in_off);
            op.out_phys = out ? (uint64_t)(uintptr_t)(out + total_out) : 0;

            ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38D_OP, &op);
            if (ret != 0) break;

            total_out += op.outlen;
            in_off += chunk;
        }

        hal_rvv_memcpy(&inst->ctx.sp38d, &op.ctx, sizeof(inst->ctx.sp38d));
    } else if (mode == MODE_CCM) {
        pufs_sp38c_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_UPDATE;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38c, sizeof(op.ctx));

        while (in_off < inlen) {
            uint32_t chunk = inlen - in_off;
            if (chunk > inst->dev->buf_size)
                chunk = inst->dev->buf_size;

            op.inlen = chunk;
            op.in_phys = (uint64_t)(uintptr_t)(in + in_off);
            op.out_phys = out ? (uint64_t)(uintptr_t)(out + total_out) : 0;

            ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38C_OP, &op);
            if (ret != 0) break;

            total_out += op.outlen;
            in_off += chunk;
        }

        hal_rvv_memcpy(&inst->ctx.sp38c, &op.ctx, sizeof(inst->ctx.sp38c));
    } else if (mode == MODE_XTS) {
        pufs_sp38e_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_UPDATE;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38e, sizeof(op.ctx));

        while (in_off < inlen) {
            uint32_t chunk = inlen - in_off;
            if (chunk > inst->dev->buf_size)
                chunk = inst->dev->buf_size;

            op.inlen = chunk;
            op.in_phys = (uint64_t)(uintptr_t)(in + in_off);
            op.out_phys = out ? (uint64_t)(uintptr_t)(out + total_out) : 0;

            ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38E_OP, &op);
            if (ret != 0) break;

            total_out += op.outlen;
            in_off += chunk;
        }

        hal_rvv_memcpy(&inst->ctx.sp38e, &op.ctx, sizeof(inst->ctx.sp38e));
    } else {
        ret = -1;
    }

    if (outlen)
        *outlen = total_out;
    drv_pufs_dev_unlock(inst->dev);
    return ret;
}

/* ===== Cipher final ===== */

int drv_pufs_cipher_final(drv_pufs_cipher_inst *inst,
                          uint8_t *out, uint32_t *outlen,
                          uint8_t *tag, uint32_t taglen)
{
    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    int ret;
    uint8_t mode = inst->mode;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    if (mode >= MODE_ECB && mode <= MODE_CTR) {
        pufs_sp38a_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_FINAL;
        op.out_phys = out ? (uint64_t)(uintptr_t)out : 0;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38a, sizeof(op.ctx));

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38A_OP, &op);
        if (ret == 0) {
            if (outlen) *outlen = op.outlen;
        }
    } else if (mode == MODE_GCM) {
        pufs_sp38d_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_FINAL;
        op.taglen = taglen;
        op.out_phys = out ? (uint64_t)(uintptr_t)out : 0;
        op.tag_phys = tag ? (uint64_t)(uintptr_t)tag : 0;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38d, sizeof(op.ctx));

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38D_OP, &op);
        if (ret == 0) {
            if (outlen) *outlen = op.outlen;
        }
    } else if (mode == MODE_CCM) {
        pufs_sp38c_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_FINAL;
        op.out_phys = out ? (uint64_t)(uintptr_t)out : 0;
        op.tag_phys = tag ? (uint64_t)(uintptr_t)tag : 0;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38c, sizeof(op.ctx));

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38C_OP, &op);
        if (ret == 0) {
            if (outlen) *outlen = op.outlen;
        }
    } else if (mode == MODE_XTS) {
        pufs_sp38e_op_t op;
        hal_rvv_memset(&op, 0, sizeof(op));
        op.op = PUFS_OP_FINAL;
        op.out_phys = out ? (uint64_t)(uintptr_t)out : 0;
        hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38e, sizeof(op.ctx));

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38E_OP, &op);
        if (ret == 0) {
            if (outlen) *outlen = op.outlen;
        }
    } else {
        goto fail;
    }

out:
    drv_pufs_dev_unlock(inst->dev);
    return ret;

fail:
    ret = -1;
    goto out;
}

/* ===== Compound: init + first update in one ioctl (GCM) ===== */

int drv_pufs_cipher_init_update(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                                pufs_skcipher_t cipher, pufs_skcipher_mode_t mode,
                                int encrypt, pufs_keytype_t keytype,
                                const uint8_t *key, uint32_t keybits,
                                const uint8_t *iv, uint32_t ivlen,
                                uint8_t *out, uint32_t *outlen,
                                const uint8_t *in, uint32_t inlen)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    inst->mode = mode;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));
    if (outlen) *outlen = 0;

    if (mode != MODE_GCM)
        return -1; /* Only GCM supported for compound init */

    if (drv_pufs_dev_lock(dev) != 0)
        return -1;

    pufs_sp38d_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_INIT_UPDATE;
    op.cipher = cipher;
    op.encrypt = encrypt;
    op.keytype = keytype;
    op.keybits = keybits;

    if (keytype == KT_SWKEY && key) {
        uint32_t klen = (keybits + 7) >> 3;
        if (klen > PUFS_SW_KEY_MAXLEN) klen = PUFS_SW_KEY_MAXLEN;
        hal_rvv_memcpy(op.key, key, klen);
    }
    if (iv && ivlen > 0) {
        if (ivlen > PUFS_BC_BLOCK_SIZE) ivlen = PUFS_BC_BLOCK_SIZE;
        hal_rvv_memcpy(op.iv, iv, ivlen);
        op.ivlen = ivlen;
    }

    op.inlen = inlen;
    op.in_phys = in ? (uint64_t)(uintptr_t)in : 0;
    op.out_phys = out ? (uint64_t)(uintptr_t)out : 0;

    int ret = drv_pufs_ioctl(dev->fd, PUFS_SP38D_OP, &op);
    if (ret == 0) {
        hal_rvv_memcpy(&inst->ctx.sp38d, &op.ctx, sizeof(inst->ctx.sp38d));
        if (outlen) *outlen = op.outlen;
    }

    drv_pufs_dev_unlock(dev);
    return ret;
}

/* ===== Compound: last update + final in one ioctl (GCM) ===== */

int drv_pufs_cipher_update_final(drv_pufs_cipher_inst *inst,
                                 uint8_t *out, uint32_t *outlen,
                                 const uint8_t *in, uint32_t inlen,
                                 uint8_t *tag, uint32_t taglen)
{
    int ret;
    uint32_t total_out = 0;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (inst->mode != MODE_GCM)
        return -1; /* Only GCM supported for compound update+final */

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    pufs_sp38d_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    hal_rvv_memcpy(&op.ctx, &inst->ctx.sp38d, sizeof(op.ctx));

    if (inlen <= inst->dev->buf_size) {
        /* Single compound ioctl */
        op.op = PUFS_OP_UPDATE_FINAL;
        op.inlen = inlen;
        op.in_phys = in ? (uint64_t)(uintptr_t)in : 0;
        op.out_phys = out ? (uint64_t)(uintptr_t)out : 0;
        op.tag_phys = tag ? (uint64_t)(uintptr_t)tag : 0;
        op.taglen = taglen;

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38D_OP, &op);
        total_out = op.outlen;
    } else {
        /* Multi-chunk: update loop, then final */
        uint32_t off = 0;

        op.op = PUFS_OP_UPDATE;
        while (off < inlen) {
            uint32_t chunk = inlen - off;
            if (chunk > inst->dev->buf_size)
                chunk = inst->dev->buf_size;

            op.inlen = chunk;
            op.in_phys = (uint64_t)(uintptr_t)(in + off);
            op.out_phys = out ? (uint64_t)(uintptr_t)(out + total_out) : 0;

            ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38D_OP, &op);
            if (ret != 0) goto done;

            total_out += op.outlen;
            off += chunk;
        }

        /* Final — ctx already in op.ctx from last update */
        op.op = PUFS_OP_FINAL;
        op.inlen = 0;
        op.taglen = taglen;
        op.out_phys = out ? (uint64_t)(uintptr_t)(out + total_out) : 0;
        op.tag_phys = tag ? (uint64_t)(uintptr_t)tag : 0;

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_SP38D_OP, &op);
        total_out += op.outlen;
    }

done:
    if (outlen) *outlen = total_out;
    drv_pufs_dev_unlock(inst->dev);
    return ret;
}
