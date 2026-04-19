/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/ioctl.h>
#include <string.h>
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

/* Internal op values matching kernel hmac_op enum */
#define HMAC_OP_AVAILABLE 0
#define HMAC_OP_HASH      1
#define HMAC_OP_HMAC      2

/* ===== Hash ===== */

int drv_pufs_hash_init(drv_pufs_hash_inst *inst, drv_pufs_inst *dev, pufs_hashtype_t hash)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));

    inst->ctx.hash = hash;
    inst->ctx.op = HMAC_OP_HASH;
    inst->ctx.start = 0;
    /* Set block length based on hash algorithm */
    if (hash >= HASH_SHA_384 && hash <= HASH_SHA_512_256)
        inst->ctx.blocklen = 128;
    else
        inst->ctx.blocklen = 64;
    inst->ctx.minlen = 1;

    return 0;
}

int drv_pufs_hash_update(drv_pufs_hash_inst *inst, const uint8_t *msg, uint32_t msglen)
{
    uint32_t offset = 0;
    int ret = 0;
    pufs_hash_op_t op;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (msglen == 0)
        return 0;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    /* Initialize op once — avoid per-chunk memset + ctx copy */
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_UPDATE;
    hal_rvv_memcpy(&op.ctx, &inst->ctx, sizeof(op.ctx));

    while (offset < msglen) {
        uint32_t chunk = msglen - offset;

        if (chunk > inst->dev->buf_size)
            chunk = inst->dev->buf_size;

        op.msglen = chunk;
        op.msg_phys = (uint64_t)(uintptr_t)(msg + offset);

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_HASH_OP, &op);
        if (ret != 0)
            goto out;

        /* ctx stays in op.ctx across iterations — no round-trip */
        offset += chunk;
    }

    /* Copy final ctx back to instance */
    hal_rvv_memcpy(&inst->ctx, &op.ctx, sizeof(inst->ctx));

out:
    drv_pufs_dev_unlock(inst->dev);
    return ret;
}

int drv_pufs_hash_final(drv_pufs_hash_inst *inst, uint8_t *dgst, uint32_t *dlen)
{
    int ret;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    pufs_hash_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_FINAL;
    hal_rvv_memcpy(&op.ctx, &inst->ctx, sizeof(op.ctx));

    op.dgst_phys = (uint64_t)(uintptr_t)dgst;

    ret = drv_pufs_ioctl(inst->dev->fd, PUFS_HASH_OP, &op);
    if (ret == 0) {
        if (dlen)
            *dlen = op.dlen;
    }

    drv_pufs_dev_unlock(inst->dev);
    return ret;
}

/* ===== HMAC ===== */

int drv_pufs_hmac_init(drv_pufs_hash_inst *inst, drv_pufs_inst *dev,
                       pufs_hashtype_t hash, pufs_keytype_t keytype,
                       const uint8_t *key, uint32_t keybits)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));

    inst->ctx.hash = hash;
    inst->ctx.op = HMAC_OP_HMAC;
    inst->ctx.keytype = keytype;
    inst->ctx.keybits = keybits;
    inst->ctx.start = 0;

    if (hash >= HASH_SHA_384 && hash <= HASH_SHA_512_256)
        inst->ctx.blocklen = 128;
    else
        inst->ctx.blocklen = 64;
    inst->ctx.minlen = inst->ctx.blocklen;

    /* Store key in context */
    if (keytype == KT_SWKEY && key) {
        uint32_t keylen = (keybits + 7) >> 3;
        if (keylen > PUFS_HMAC_BLOCK_MAXLEN)
            keylen = PUFS_HMAC_BLOCK_MAXLEN;
        hal_rvv_memcpy(inst->ctx.key, key, keylen);
    } else {
        /* For HW keys, store slot index */
        inst->ctx.keyslot = (uint32_t)(uintptr_t)key;
    }

    return 0;
}

int drv_pufs_hmac_update(drv_pufs_hash_inst *inst, const uint8_t *msg, uint32_t msglen)
{
    uint32_t offset = 0;
    int ret = 0;
    pufs_hmac_op_t op;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (msglen == 0)
        return 0;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    /* Initialize op once */
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_UPDATE;
    op.hash = inst->ctx.hash;
    op.keytype = inst->ctx.keytype;
    op.keybits = inst->ctx.keybits;
    hal_rvv_memcpy(&op.ctx, &inst->ctx, sizeof(op.ctx));

    while (offset < msglen) {
        uint32_t chunk = msglen - offset;

        if (chunk > inst->dev->buf_size)
            chunk = inst->dev->buf_size;

        op.msglen = chunk;
        op.msg_phys = (uint64_t)(uintptr_t)(msg + offset);

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_HMAC_OP, &op);
        if (ret != 0)
            goto out;

        offset += chunk;
    }

    hal_rvv_memcpy(&inst->ctx, &op.ctx, sizeof(inst->ctx));

out:
    drv_pufs_dev_unlock(inst->dev);
    return ret;
}

int drv_pufs_hmac_final(drv_pufs_hash_inst *inst, uint8_t *dgst, uint32_t *dlen)
{
    int ret;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    pufs_hmac_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_FINAL;
    op.hash = inst->ctx.hash;
    op.keytype = inst->ctx.keytype;
    op.keybits = inst->ctx.keybits;
    hal_rvv_memcpy(&op.ctx, &inst->ctx, sizeof(op.ctx));

    op.dgst_phys = (uint64_t)(uintptr_t)dgst;

    ret = drv_pufs_ioctl(inst->dev->fd, PUFS_HMAC_OP, &op);
    if (ret == 0) {
        if (dlen)
            *dlen = op.dlen;
    }

    drv_pufs_dev_unlock(inst->dev);
    return ret;
}

/* ===== CMAC ===== */

int drv_pufs_cmac_init(drv_pufs_cmac_inst *inst, drv_pufs_inst *dev,
                       pufs_skcipher_t cipher, pufs_keytype_t keytype,
                       const uint8_t *key, uint32_t keybits)
{
    if (!inst || !dev || dev->fd < 0)
        return -1;

    inst->dev = dev;
    hal_rvv_memset(&inst->ctx, 0, sizeof(inst->ctx));

    inst->ctx.cipher = cipher;
    inst->ctx.keytype = keytype;
    inst->ctx.keybits = keybits;
    inst->ctx.op = 1; /* CMAC_CMAC */
    inst->ctx.start = 0;
    inst->ctx.minlen = 1;

    if (keytype == KT_SWKEY && key) {
        uint32_t keylen = (keybits + 7) >> 3;
        if (keylen > PUFS_SW_KEY_MAXLEN)
            keylen = PUFS_SW_KEY_MAXLEN;
        hal_rvv_memcpy(inst->ctx.key, key, keylen);
    } else {
        inst->ctx.keyslot = (uint32_t)(uintptr_t)key;
    }

    return 0;
}

int drv_pufs_cmac_update(drv_pufs_cmac_inst *inst, const uint8_t *msg, uint32_t msglen)
{
    uint32_t offset = 0;
    int ret = 0;
    pufs_cmac_op_t op;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (msglen == 0)
        return 0;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    /* Initialize op once */
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_UPDATE;
    op.cipher = inst->ctx.cipher;
    op.keytype = inst->ctx.keytype;
    op.keybits = inst->ctx.keybits;
    hal_rvv_memcpy(&op.ctx, &inst->ctx, sizeof(op.ctx));

    while (offset < msglen) {
        uint32_t chunk = msglen - offset;

        if (chunk > inst->dev->buf_size)
            chunk = inst->dev->buf_size;

        op.msglen = chunk;
        op.msg_phys = (uint64_t)(uintptr_t)(msg + offset);

        ret = drv_pufs_ioctl(inst->dev->fd, PUFS_CMAC_OP, &op);
        if (ret != 0)
            goto out;

        offset += chunk;
    }

    hal_rvv_memcpy(&inst->ctx, &op.ctx, sizeof(inst->ctx));

out:
    drv_pufs_dev_unlock(inst->dev);
    return ret;
}

int drv_pufs_cmac_final(drv_pufs_cmac_inst *inst, uint8_t *dgst, uint32_t *dlen)
{
    int ret;

    if (!inst || !inst->dev || inst->dev->fd < 0)
        return -1;

    if (drv_pufs_dev_lock(inst->dev) != 0)
        return -1;

    pufs_cmac_op_t op;
    hal_rvv_memset(&op, 0, sizeof(op));
    op.op = PUFS_OP_FINAL;
    op.cipher = inst->ctx.cipher;
    op.keytype = inst->ctx.keytype;
    op.keybits = inst->ctx.keybits;
    hal_rvv_memcpy(&op.ctx, &inst->ctx, sizeof(op.ctx));

    op.dgst_phys = (uint64_t)(uintptr_t)dgst;

    ret = drv_pufs_ioctl(inst->dev->fd, PUFS_CMAC_OP, &op);
    if (ret == 0) {
        if (dlen)
            *dlen = op.dlen;
    }

    drv_pufs_dev_unlock(inst->dev);
    return ret;
}
