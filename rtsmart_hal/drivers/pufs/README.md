# drv_pufs — PUFsecurity Secure Engine HAL Driver

Userspace Hardware Abstraction Layer for the PUFsecurity Secure Engine (PUF SE)
on the Kendryte K230 SoC, running under RT-Smart RTOS.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  Application / mbedtls ALT layer                     │
├──────────────────────────────────────────────────────┤
│  drv_pufs HAL  (this library — libpufs.a)            │
│  ┌──────────┐ ┌──────────────┐ ┌──────────────────┐ │
│  │ drv_pufs │ │ drv_pufs_hash│ │ drv_pufs_cipher  │ │
│  │ (core)   │ │ (hash/hmac)  │ │ (sym encrypt)    │ │
│  ├──────────┤ ├──────────────┤ ├──────────────────┤ │
│  │ drv_pufs_asym (ECDSA, ECDH, RSA, SM2, key mgmt)│ │
│  └─────────────────────┬────────────────────────────┘│
│                        │ ioctl(/dev/pufs)             │
├────────────────────────┼─────────────────────────────┤
│  Kernel: puf_fw driver │                              │
│  ┌─────────────────────┴────────────────────────────┐│
│  │ drv_pufs.c  (device registration, ioctl dispatch)││
│  │ ┌────────┐┌────────┐┌───────┐┌───────┐┌───────┐ ││
│  │ │pufs_ka ││pufs_ecp││pufs_  ││pufs_  ││pufs_  │ ││
│  │ │(KWP/   ││(ECC    ││sp38a  ││sp38d  ││hmac   │ ││
│  │ │key mgr)││engine) ││(AES)  ││(GCM)  ││(hash) │ ││
│  │ └────────┘└────────┘└───────┘└───────┘└───────┘ ││
│  │     + pufs_sp38c(CCM) pufs_sp38e(XTS) pufs_sm2  ││
│  │     + pufs_kdf pufs_sp90a(DRBG) pufs_rt pufs_dma││
│  └──────────────────────────────────────────────────┘│
│                        │ MMIO                         │
├────────────────────────┼─────────────────────────────┤
│  PUF SE Hardware       │                              │
│  Base + offsets:                                      │
│  0x000 DMA  0x100 Crypto  0x200 SP38A  0x300 KWP     │
│  0x800 HMAC 0x900 KDF  0xB00 DRBG  0xC00 KA          │
│  0x1000 PKC(ECC/RSA)  0x3000 PUFrt  0x4000 CDE       │
└──────────────────────────────────────────────────────┘
```

### Design Principles

- **Stateless kernel driver.** All cryptographic context (hash state, cipher
  state, IV, partial blocks) lives in userspace structs. Each ioctl call
  sends the context in, the kernel restores HW state, performs the operation,
  snapshots the new state back, and releases the hardware lock.

- **Per-instance thread safety.** Each `drv_pufs_inst` has a `pthread_mutex_t`
  (`io_lock`) that serializes concurrent calls from different threads through
  the same device handle.

- **Kernel HW lock.** A single RT-Thread mutex in the kernel protects the
  physical hardware. It is held only for the duration of each atomic
  operation or streaming step — not across an entire multi-step session.

## Files

| File | Purpose |
|------|---------|
| `drv_pufs.c` | Device open/close, ioctl wrapper, lock management |
| `drv_pufs.h` | Public API, struct definitions, enums, constants |
| `drv_pufs_hash.c` | Hash (SHA-*) and HMAC init/update/final |
| `drv_pufs_cipher.c` | Symmetric cipher init/update/final (CBC, CTR, GCM, CCM, XTS) |
| `drv_pufs_asym.c` | ECC, ECDSA, ECDH, RSA, SM2, key import/export/derive |
| `Makefile` | Builds `libpufs.a`, installs headers |

## Usage

### Initialization

```c
#include "drv_pufs.h"

drv_pufs_inst dev;
if (drv_pufs_open(&dev) != 0) {
    /* PUF SE unavailable */
}

/* ... use dev for operations ... */

drv_pufs_close(&dev);
```

Multiple threads may share a single `drv_pufs_inst`; the internal mutex
serializes access. Alternatively, each thread can open its own instance.

### Hashing (SHA-256 example)

```c
drv_pufs_hash_inst hash;
uint8_t digest[32];
uint32_t dlen;

drv_pufs_hash_init(&hash, &dev, HASH_SHA_256);
drv_pufs_hash_update(&hash, data, data_len);
drv_pufs_hash_final(&hash, digest, &dlen);
```

### Symmetric Encryption (AES-CBC example)

```c
drv_pufs_cipher_inst cipher;
uint8_t out[4096];
uint32_t outlen;

drv_pufs_cipher_init(&cipher, &dev, SK_AES, MODE_CBC, 1 /* encrypt */,
                     KT_SWKEY, key, 256, iv, 16);
drv_pufs_cipher_update(&cipher, out, &outlen, plaintext, plaintext_len);
drv_pufs_cipher_final(&cipher, out + outlen, &outlen, NULL, 0);
```

### AEAD (AES-GCM example)

```c
drv_pufs_cipher_inst cipher;

drv_pufs_cipher_init(&cipher, &dev, SK_AES, MODE_GCM, 1,
                     KT_SWKEY, key, 128, iv, 12);
/* AAD: pass with NULL output */
drv_pufs_cipher_update(&cipher, NULL, NULL, aad, aad_len);
/* Encrypt */
drv_pufs_cipher_update(&cipher, ciphertext, &ct_len, plaintext, pt_len);
/* Finalize and get tag */
drv_pufs_cipher_final(&cipher, NULL, NULL, tag, 16);
```

### AEAD fast path (AES-GCM compound ops)

For GCM, compound operations reduce 4 ioctls down to 2:

```c
drv_pufs_cipher_inst cipher;
uint32_t outlen;

/* Init + AAD in one ioctl */
drv_pufs_cipher_init_update(&cipher, &dev, SK_AES, MODE_GCM, 1,
                            KT_SWKEY, key, 128, iv, 12,
                            NULL, NULL, aad, aad_len);
/* Encrypt + finalize + tag in one ioctl */
drv_pufs_cipher_update_final(&cipher, ciphertext, &outlen,
                             plaintext, pt_len, tag, 16);
```

The compound functions are GCM-only. They return `-1` for other modes,
allowing callers to fall back to the streaming API.

### ECDSA Sign / Verify

```c
pufs_ecdsa_sig_t sig;

/* Sign: private key must be in a PRK slot */
drv_pufs_key_import_plaintext(&dev, KT_PRKEY, KS_PRK_0, priv_key, 256);
drv_pufs_ecdsa_sign(&dev, ECC_NISTP256, KT_PRKEY, KS_PRK_0,
                    digest, 32, &sig);
drv_pufs_key_clear(&dev, KT_PRKEY, KS_PRK_0, 256);

/* Verify: uses public key directly */
pufs_ecc_puk_t puk = { .qlen = 32 };
memcpy(puk.x, pub_x, 32);
memcpy(puk.y, pub_y, 32);
drv_pufs_ecdsa_verify(&dev, ECC_NISTP256, digest, 32, &puk, &sig);
```

### ECDH Key Exchange

```c
pufs_ecc_puk_t my_pub;

/* Generate ephemeral key pair (private stays in HW slot) */
drv_pufs_ecc_prk_gen(&dev, ECC_NISTP256, 1 /* ephemeral */, KS_PRK_0);
drv_pufs_ecc_puk_gen(&dev, ECC_NISTP256, KT_PRKEY, KS_PRK_0, &my_pub);

/* Compute shared secret with peer's public key */
uint8_t shared[32];
drv_pufs_ecc_cdh(&dev, ECC_NISTP256, 1, KS_PRK_0,
                 &peer_pub, shared, 32);

/* Clear slot */
drv_pufs_key_clear(&dev, KT_PRKEY, KS_PRK_0, 256);
```

### Key Management

```c
/* Import a plaintext key into session slot */
drv_pufs_key_import_plaintext(&dev, KT_SSKEY, KS_SK256_0, key, 256);

/* Export a key from slot */
uint8_t exported[32];
drv_pufs_key_export_plaintext(&dev, KT_SSKEY, KS_SK256_0, exported, 256);

/* Clear a key slot */
drv_pufs_key_clear(&dev, KT_SSKEY, KS_SK256_0, 256);

/* Read hardware RNG */
uint8_t random_bytes[64];
drv_pufs_rng_read(&dev, random_bytes, 64);
```

## API Reference

### Device Management

| Function | Description |
|----------|-------------|
| `drv_pufs_open(inst)` | Open `/dev/pufs`, initialize mutex |
| `drv_pufs_close(inst)` | Close device, destroy mutex |
| `drv_pufs_dev_lock(inst)` | Acquire per-instance mutex |
| `drv_pufs_dev_unlock(inst)` | Release per-instance mutex |

### Hash / HMAC

| Function | Description |
|----------|-------------|
| `drv_pufs_hash_init(inst, dev, hash)` | Start hash session |
| `drv_pufs_hash_update(inst, msg, len)` | Feed data |
| `drv_pufs_hash_final(inst, dgst, dlen)` | Finalize and get digest |
| `drv_pufs_hmac_init(inst, dev, hash, keytype, key, keybits)` | Start HMAC |
| `drv_pufs_hmac_update(inst, msg, len)` | Feed data |
| `drv_pufs_hmac_final(inst, dgst, dlen)` | Finalize and get MAC |

Supported hash types: `HASH_SHA_224`, `HASH_SHA_256`, `HASH_SHA_384`,
`HASH_SHA_512`, `HASH_SHA_512_224`, `HASH_SHA_512_256`, `HASH_SM3`.

### CMAC

| Function | Description |
|----------|-------------|
| `drv_pufs_cmac_init(inst, dev, cipher, keytype, key, keybits)` | Start CMAC |
| `drv_pufs_cmac_update(inst, msg, len)` | Feed data |
| `drv_pufs_cmac_final(inst, dgst, dlen)` | Finalize and get MAC |

Supported ciphers: `SK_AES`, `SK_SM4`.

### Symmetric Cipher

| Function | Description |
|----------|-------------|
| `drv_pufs_cipher_init(...)` | Init generic cipher (ECB/CBC/CFB/OFB/CTR) |
| `drv_pufs_cipher_ccm_init(...)` | Init CCM with nonce/lengths/tag size |
| `drv_pufs_cipher_xts_init(...)` | Init XTS with two keys |
| `drv_pufs_cipher_update(inst, out, outlen, in, inlen)` | Process data |
| `drv_pufs_cipher_final(inst, out, outlen, tag, taglen)` | Finalize (+ tag for AEAD) |
| `drv_pufs_cipher_init_update(inst, dev, ...)` | Compound: init + first update in one ioctl (GCM only) |
| `drv_pufs_cipher_update_final(inst, out, outlen, in, inlen, tag, taglen)` | Compound: last update + final in one ioctl (GCM only) |

Cipher modes: `MODE_ECB`, `MODE_CBC_CS0`–`MODE_CBC_CS3`, `MODE_CFB`,
`MODE_OFB`, `MODE_CTR32`/`MODE_CTR64`/`MODE_CTR128`, `MODE_GCM`,
`MODE_CCM`, `MODE_XTS`.

### Asymmetric — ECDSA

| Function | Description |
|----------|-------------|
| `drv_pufs_ecdsa_sign(dev, ecctype, prktype, prkslot, md, mdlen, sig)` | Sign digest |
| `drv_pufs_ecdsa_verify(dev, ecctype, md, mdlen, puk, sig)` | Verify signature |

### Asymmetric — ECC Key Operations

| Function | Description |
|----------|-------------|
| `drv_pufs_ecc_prk_gen(dev, ecctype, is_ephemeral, prkslot)` | Generate private key |
| `drv_pufs_ecc_puk_gen(dev, ecctype, prktype, prkslot, puk)` | Derive public key |
| `drv_pufs_ecc_puk_verify(dev, ecctype, puk)` | Validate public key on curve |
| `drv_pufs_ecc_cdh(dev, ecctype, is_ephemeral, prkslot, puk, out, outlen)` | ECDH CDH |

Supported curves: `ECC_NISTP192`, `ECC_NISTP224`, `ECC_NISTP256`,
`ECC_NISTP384`, `ECC_NISTP521`, `ECC_SM2`.

### Asymmetric — RSA

| Function | Description |
|----------|-------------|
| `drv_pufs_rsa_sign(dev, mode, rsatype, hashtype, puk, n, prk, msg, msglen, salt, saltlen, sig)` | RSA sign |
| `drv_pufs_rsa_verify(dev, mode, rsatype, hashtype, puk, n, msg, msglen, sig)` | RSA verify |

RSA modes: `RSA_BASE`, `RSA_X931`, `RSA_PKCS1_V15`, `RSA_PSS`.
RSA sizes: `RSA_1024`, `RSA_2048`, `RSA_3072`, `RSA_4096`.

### Asymmetric — SM2

| Function | Description |
|----------|-------------|
| `drv_pufs_sm2_sign(dev, prktype, prkslot, id, idlen, msg, msglen, sig)` | SM2 sign |
| `drv_pufs_sm2_verify(dev, puk, id, idlen, msg, msglen, sig)` | SM2 verify |
| `drv_pufs_sm2_enc(dev, format, in, inlen, out, outlen, puk)` | SM2 encrypt |
| `drv_pufs_sm2_dec(dev, prkslot, in, inlen, out, outlen)` | SM2 decrypt |
| `drv_pufs_sm2_kex(dev, init, prkslotl, tprkslotl, pukr, tpukr, idl, idllen, idr, idrlen, key, keybits, dgst2, dlen2, dgst3, dlen3)` | SM2 key exchange |

### Key Management

| Function | Description |
|----------|-------------|
| `drv_pufs_key_import_plaintext(dev, keytype, slot, key, keybits)` | Import plaintext key to slot |
| `drv_pufs_key_export_plaintext(dev, keytype, slot, key, keybits)` | Export key from slot |
| `drv_pufs_key_clear(dev, keytype, slot, keybits)` | Wipe a key slot |
| `drv_pufs_key_derive(dev, ...)` | KDF (PBKDF, KBKDF) |

Key types: `KT_SWKEY` (software), `KT_OTPKEY` (OTP-stored), `KT_PUFKEY`
(PUF-derived), `KT_RANDKEY` (HW RNG), `KT_SHARESEC` (shared secret),
`KT_SSKEY` (session), `KT_PRKEY` (private ECC).

Key slots:
- Session: `KS_SK128_0`–`KS_SK128_7`, `KS_SK256_0`–`KS_SK256_3`,
  `KS_SK512_0`–`KS_SK512_1`
- Private ECC: `KS_PRK_0`–`KS_PRK_2` (max 576 bits each)
- Shared secret: `KS_SHARESEC_0`

### OTP / PUFrt

| Function | Description |
|----------|-------------|
| `drv_pufs_otp_read(dev, addr, buf, len)` | Read OTP memory |
| `drv_pufs_otp_write(dev, addr, buf, len)` | Write OTP memory (irreversible) |
| `drv_pufs_otp_lock(dev, addr, len, lock)` | Lock OTP region |
| `drv_pufs_otp_get_rwlck(dev, addr, lock)` | Query OTP lock status |
| `drv_pufs_key_to_otp(dev, slot, key, keybits)` | Program key into OTP |
| `drv_pufs_otp_apply_security_config(dev, spi2axi, jtag, secure_boot, isp)` | Program OTP security config bits |
| `drv_pufs_otp_get_security_config_state(dev, state)` | Read OTP security config state and word locks |
| `drv_pufs_otp_lock_security_config_words(dev)` | Lock OTP security config words read-only |
| `drv_pufs_rt_version(dev, version, features)` | Query PUFrt version |

### RNG / DRBG

| Function | Description |
|----------|-------------|
| `drv_pufs_rng_read(dev, buf, len)` | Read raw hardware RNG |
| `drv_pufs_drbg_instantiate(dev, mode, security, df, nonce, noncelen, pstr, pstrlen)` | Init DRBG |
| `drv_pufs_drbg_reseed(dev, df, adin, adinlen)` | Reseed DRBG |
| `drv_pufs_drbg_generate(dev, out, outbits, pr, df, adin, adinlen)` | Generate random |
| `drv_pufs_drbg_uninstantiate(dev)` | Destroy DRBG instance |

## Data Types

### Public Key

```c
#define PUFS_QLEN_MAX 72

typedef struct {
    uint32_t qlen;              /* Coordinate byte length */
    uint8_t  x[PUFS_QLEN_MAX]; /* X coordinate, big-endian */
    uint8_t  y[PUFS_QLEN_MAX]; /* Y coordinate, big-endian */
} pufs_ecc_puk_t;
```

### ECDSA Signature

```c
#define PUFS_NLEN_MAX 72

typedef struct {
    uint32_t qlen;              /* Component byte length */
    uint8_t  r[PUFS_NLEN_MAX]; /* r component, big-endian */
    uint8_t  s[PUFS_NLEN_MAX]; /* s component, big-endian */
} pufs_ecdsa_sig_t;
```

### Device Instance

```c
typedef struct {
    int fd;                     /* File descriptor for /dev/pufs */
    uint32_t buf_size;          /* DMA buffer size (65536) */
    pthread_mutex_t io_lock;    /* Per-instance thread safety lock */
    int io_lock_init;           /* Lock initialized flag */
} drv_pufs_inst;
```

## Known Limitations

| Limitation | Detail |
|-----------|--------|
| **KWP key import max** | Plaintext key import via KWP only works for keys ≤ 256 bits. P-384/P-521 private keys cannot be imported. |
| **SW_KEY_MAXLEN** | Kernel buffer for key I/O is 64 bytes. Keys > 512 bits are silently truncated (affects P-521 = 528 bits). |
| **DLEN_MAX** | Kernel digest buffer is 64 bytes. Digests > 64 bytes passed to ECDSA verify are truncated, causing false verification failures. |
| **Single HW instance** | One PUF SE per SoC. All operations serialize on the kernel mutex. |
| **PRK slots** | Only 3 private key slots (PRK_0–PRK_2). Concurrent ECC operations must coordinate slot usage. |
| **DMA buffer** | 64 KB per direction. Single cipher update calls cannot exceed this. |
| **No key isolation** | `key_import_plaintext` copies plaintext key through userspace memory and kernel buffers before loading into HW slot. The key is not protected in transit. |

## Kernel Driver

The kernel-side driver is at:
```
src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/interdrv/cipher/puf_fw/
```

Sub-modules:

| Directory | HW Block | Function |
|-----------|----------|----------|
| `pufs_dma/` | 0x000 | DMA engine for bulk data transfer |
| `pufs_crypto/` | 0x100 | Low-level crypto register interface |
| `pufs_sp38a/` | 0x200 | AES/SM4 ECB/CBC/CFB/OFB/CTR modes |
| `pufs_cmac/` | 0x220 | CMAC authentication |
| `pufs_sp38c/` | 0x240 | AES-CCM authenticated encryption |
| `pufs_sp38d/` | 0x260 | AES-GCM authenticated encryption |
| `pufs_sp38e/` | 0x280 | AES-XTS disk encryption mode |
| `pufs_ka/` | 0x300 | Key Authority — KWP key wrap/unwrap |
| `pufs_hmac/` | 0x800 | SHA hash and HMAC engine |
| `pufs_kdf/` | 0x900 | Key derivation functions |
| `pufs_sp90a/` | 0xB00 | SP800-90A DRBG |
| `pufs_ecc/` | — | ECC curve parameters and math |
| `pufs_ecp/` | 0x1000 | ECC point operations (PKC engine) |
| `pufs_rt/` | 0x3000 | PUFrt — PUF core, OTP, enrollment |
| `pufs_sm2/` | — | SM2 algorithm (uses PKC engine) |
| `pufs_common/` | — | Shared utilities, logging, register helpers |

Device registered as `"pufs"` via `INIT_DEVICE_EXPORT(pufs_device_init)`,
accessible at `/dev/pufs` with `RT_DEVICE_FLAG_RDWR`.

### ioctl Commands

| Command | Code | Description |
|---------|------|-------------|
| `PUFS_UID_GET` | 0x00 | Read device unique ID |
| `PUFS_OTP_READ` | 0x01 | Read OTP memory |
| `PUFS_OTP_WRITE` | 0x02 | Write OTP memory |
| `PUFS_OTP_LOCK` | 0x03 | Lock OTP region |
| `PUFS_RNG_READ` | 0x04 | Read hardware RNG |
| `PUFS_OTP_RWLCK_GET` | 0x05 | Query OTP lock status |
| `PUFS_RT_VERSION` | 0x06 | PUFrt version/features |
| `PUFS_KEY2OTP` | 0x07 | Program key to OTP |
| `PUFS_OTP_SEC_CFG` | 0x0A | Program OTP security config bits |
| `PUFS_OTP_SEC_LOCK` | 0x0B | Lock OTP security config words |
| `PUFS_OTP_SEC_STATE` | 0x0C | Query OTP security config state |
| `PUFS_KEY_INOUT` | 0x10 | Key import/export/clear |
| `PUFS_KEY_DERIVE` | 0x11 | Key derivation |
| `PUFS_ECC_PRK_GEN` | 0x40 | ECC private key generation |
| `PUFS_ECC_PUK_GEN` | 0x41 | ECC public key derivation |
| `PUFS_ECC_PUK_VERIFY` | 0x42 | ECC public key validation |
| `PUFS_ECC_CDH` | 0x43 | ECDH shared secret |
| `PUFS_ECDSA_SIGN` | 0x45 | ECDSA signature |
| `PUFS_ECDSA_VERIFY` | 0x46 | ECDSA verification |
| `PUFS_SM2_SIGN` | 0x48 | SM2 signature |
| `PUFS_SM2_VERIFY` | 0x49 | SM2 verification |
| `PUFS_SM2_ENC` | 0x4A | SM2 encryption |
| `PUFS_SM2_DEC` | 0x4B | SM2 decryption |
| `PUFS_SM2_KEX` | 0x4C | SM2 key exchange |
| `PUFS_RSA_SIGN` | 0x4E | RSA signature |
| `PUFS_RSA_VERIFY` | 0x4F | RSA verification |
| `PUFS_HASH_OP` | 0x60 | Hash init/update/final |
| `PUFS_HMAC_OP` | 0x61 | HMAC init/update/final |
| `PUFS_CMAC_OP` | 0x62 | CMAC init/update/final |
| `PUFS_SP38A_OP` | 0x63 | AES/SM4 block cipher modes |
| `PUFS_SP38D_OP` | 0x64 | AES-GCM |
| `PUFS_SP38C_OP` | 0x65 | AES-CCM |
| `PUFS_SP38E_OP` | 0x66 | AES-XTS |
| `PUFS_DRBG_INIT` | 0x70 | DRBG instantiate |
| `PUFS_DRBG_RESEED` | 0x71 | DRBG reseed |
| `PUFS_DRBG_GENERATE` | 0x72 | DRBG generate |
| `PUFS_DRBG_UNINIT` | 0x73 | DRBG uninstantiate |

### Compound ioctl Operations

The streaming crypto ioctls (`PUFS_SP38D_OP` for GCM) support compound
op types that merge two steps into a single kernel call, eliminating
context serialization and syscall overhead between them:

| Op Type | Value | Description |
|---------|-------|-------------|
| `PUFS_OP_INIT` | 0 | Initialize cipher state |
| `PUFS_OP_UPDATE` | 1 | Process data chunk |
| `PUFS_OP_FINAL` | 2 | Finalize and produce tag |
| `PUFS_OP_INIT_UPDATE` | 3 | Init + first update in one call |
| `PUFS_OP_UPDATE_FINAL` | 4 | Last update + final in one call |

`PUFS_OP_INIT_UPDATE` skips context serialization between init and the
first update (e.g. AAD feed). `PUFS_OP_UPDATE_FINAL` skips serialization
between the last data chunk and finalization. Together they reduce a
typical GCM encrypt/decrypt from 4 ioctls to 2.

For data larger than the DMA buffer (64 KB), `drv_pufs_cipher_update_final`
automatically falls back to multi-chunk updates followed by a separate final.

### Error Codes

HAL functions return `0` on success. On failure, they return the negated
PUF SE status code (`-ret`). Common values:

| Status | Meaning |
|--------|---------|
| `E_INVALID` | Invalid parameter or slot |
| `E_BUSY` | Hardware busy |
| `E_DENY` | Access denied (key slot locked) |
| `E_OVERFLOW` | Key too large for slot |
| `E_UNDERFLOW` | Insufficient data |
| `E_VERFAIL` | Signature / tag verification failed |
| `E_ECMPROG` | ECC microprogram error |
| `E_ERROR` | Unspecified hardware error |
| `E_UNSUPPORT` | Operation not supported for this curve/mode |
