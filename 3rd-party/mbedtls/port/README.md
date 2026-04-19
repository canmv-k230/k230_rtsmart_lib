# Mbed TLS Hardware Acceleration Port for K230 (PUFsecurity SE)

## Overview

This port accelerates Mbed TLS 3.6.3 cryptographic operations using the
PUFsecurity Secure Engine (PUF SE) integrated into the Kendryte K230 SoC.
The PUF SE is accessed through an RT-Smart kernel driver (`/dev/pufs`) with a
userspace HAL (`drv_pufs`) that serializes operations via hardware locks.

### Configuration

Hardware acceleration is enabled by compiling with:

```
-DMBEDTLS_USER_CONFIG_FILE="mbedtls_port_config.h"
```

This header is the *full* Mbed TLS configuration (not a delta). It defines
which `MBEDTLS_*_ALT` macros are active.

### Files

| File | Purpose |
|------|---------|
| `mbedtls_port_config.h` | Full Mbed TLS configuration with ALT enables |
| `pufs_aes_alt.c`, `aes_alt.h` | AES block cipher (CBC, CTR) |
| `pufs_ccm_alt.c`, `ccm_alt.h` | AES-CCM authenticated encryption |
| `pufs_cmac_alt.c`, `cmac_alt.h` | AES-CMAC / DES-CMAC message authentication |
| `pufs_gcm_alt.c`, `gcm_alt.h` | AES-GCM authenticated encryption |
| `pufs_sha256_alt.c`, `sha256_alt.h` | SHA-224 / SHA-256 hashing |
| `pufs_sha512_alt.c`, `sha512_alt.h` | SHA-384 / SHA-512 hashing |
| `pufs_ecdsa_alt.c` | ECDSA sign & verify |
| `pufs_ecdh_alt.c` | ECDH key generation & shared secret computation |
| `pufs_aes_sw.c/h` | Embedded SW AES tables (for ECB / small-block fallback) |
| `pufs_sha256_sw.c/h` | Embedded SW SHA-256 (for small-input / clone fallback) |
| `timing.c`, `timing_alt.h` | RT-Smart timing support |
| `net_sockets.c` | RT-Smart network socket support |
| `platform_util.c` | Platform utility functions |

---

## Module Details

### AES (`MBEDTLS_AES_ALT`)

| Mode | Acceleration | Notes |
|------|-------------|-------|
| CBC | **HW** (with SW fallback) | HW when `length >= 4096` bytes |
| CTR | **HW** (with SW fallback) | HW when `length >= 4096` bytes AND `nc_off == 0` |
| ECB | SW only | Single-block; HW overhead not worthwhile |
| CFB-128, CFB-8 | SW only | Not implemented in HW driver |
| OFB | SW only | Not implemented in HW driver |
| XTS | SW only | Not implemented in HW driver |

**Benefits:**
- Significant throughput improvement for bulk encryption (TLS record
  payloads, file encryption) in CBC and CTR modes.
- SW fallback for small blocks avoids the overhead of kernel ioctl +
  hardware lock acquisition on operations where HW would be slower.

**Limitations:**
- The 4096-byte threshold (`MBEDTLS_PUFS_AES_HW_THRESHOLD`) means short
  TLS records (e.g. handshake messages, small HTTP responses) use SW.
- CTR mode requires `nc_off == 0` (block-aligned); mid-block CTR calls
  always use SW.
- Key material is stored in the context struct in user memory; the HW does
  not provide key isolation for AES.

---

### AES-CCM (`MBEDTLS_CCM_ALT`)

| Operation | Acceleration |
|-----------|-------------|
| Encrypt + Tag | **HW only** |
| Decrypt + Verify | **HW only** |
| Streaming (starts/update/finish) | **HW only** |

**Benefits:**
- Full hardware acceleration for all CCM operations.
- Tag verification performed atomically in hardware; no timing leakage on
  tag comparison.

**Limitations:**
- **No SW fallback.** If the PUF SE device cannot be opened, all CCM
  operations fail.
- Nonce length restricted to 7–13 bytes (per RFC 3610).

---

### AES-GCM (`MBEDTLS_GCM_ALT`)

| Operation | Acceleration |
|-----------|-------------|
| Encrypt + Tag | **HW only** |
| Decrypt + Verify | **HW only** |
| One-shot (crypt_and_tag / auth_decrypt) | **HW** — 2 ioctls via compound ops |
| Streaming (starts/update_ad/update/finish) | **HW** — 4 ioctls |

**Benefits:**
- Full hardware acceleration including GHASH.
- One-shot operations use compound ioctls (`PUFS_OP_INIT_UPDATE` +
  `PUFS_OP_UPDATE_FINAL`) to merge init+AAD and data+finalize into 2
  kernel calls instead of 4, cutting syscall overhead in half.
- Falls back to the 4-ioctl streaming path automatically if compound ops
  fail (e.g. on older kernel builds without compound op support).
- Output buffer zeroed on authentication failure (defense-in-depth).

**Limitations:**
- **No SW fallback.** PUF SE device must be available.
- Tag length: 4–16 bytes.

---

### CMAC (`MBEDTLS_CMAC_ALT`)

| Cipher | Acceleration |
|--------|-------------|
| AES-128/192/256 | **HW only** |
| DES3 (3DES) | SW only |

**Benefits:**
- Hardware CMAC for AES avoids SW subkey derivation and block processing.

**Limitations:**
- AES CMAC has **no SW fallback**; if HW fails, the operation fails.
- DES3-CMAC uses a fully software implementation.

---

### SHA-256 / SHA-224 (`MBEDTLS_SHA256_ALT`)

| Input Size | Acceleration |
|-----------|-------------|
| ≤ 256 bytes total | SW only (buffered) |
| > 256 bytes total | **HW** (with SW context kept in sync) |

**Benefits:**
- Large hashes (certificate verification, firmware hashing) offloaded to HW.
- SW context maintained in parallel, so `mbedtls_sha256_clone()` always
  works (HW hash sessions cannot be cloned in the kernel).

**Limitations:**
- The 256-byte threshold (`MBEDTLS_PUFS_SHA256_SW_FALLBACK_THRESHOLD`)
  means small hashes (HMAC keys, short messages) stay in SW.
- Cloned contexts are forced back to SW-only (`MODE_BUFFERED`).

---

### SHA-512 / SHA-384 (`MBEDTLS_SHA512_ALT`)

| Operation | Acceleration |
|-----------|-------------|
| All | **HW only** |

**Benefits:**
- All SHA-512/384 operations use hardware, including TLS 1.3 transcript
  hashes and certificate chains.

**Limitations:**
- **No SW fallback.** PUF SE device must be available.
- `mbedtls_sha512_clone()` copies the device handle and reinitializes; the
  cloned context restarts from scratch (accumulated state is not preserved
  across clone — only safe if clone happens before first update).

---

### ECDSA (`MBEDTLS_ECDSA_SIGN_ALT`, `MBEDTLS_ECDSA_VERIFY_ALT`)

#### Supported Curves

| Curve | Sign | Verify |
|-------|------|--------|
| secp192r1 (P-192) | **HW** | **HW**, SW fallback on failure |
| secp224r1 (P-224) | **HW** | **HW**, SW fallback on failure |
| secp256r1 (P-256) | **HW** | **HW**, SW fallback on failure |
| secp384r1 (P-384) | SW only | **HW**, SW fallback on failure |
| secp521r1 (P-521) | SW only | **HW**, SW fallback on failure |
| brainpool, secp*k1, etc. | SW only | SW only |

#### Sign Path

1. Look up curve in HW map.
2. If curve found **and** key size ≤ 256 bits (qlen ≤ 32): import private
   key to PRK_0 slot via KWP, sign in HW, clear slot.
3. Otherwise: SW sign using mbedtls's constant-time scalar multiplication.

**Benefits:**
- P-256 ECDSA sign (the most common TLS curve) is fully hardware-accelerated
  with hardware RNG for nonce `k` generation.
- Private key material in the HW slot is wiped immediately after signing.

**Limitations:**
- P-384 and P-521 always use SW sign. The KWP (Key Wrap Protocol) hardware
  does not support plaintext key import for private keys larger than 256 bits.
  Additionally, the kernel's `SW_KEY_MAXLEN` (64 bytes) would truncate P-521's
  66-byte private key.
- The private key is still in user-space memory (as an MPI) before import;
  HW does not provide end-to-end key isolation for imported keys.

#### Verify Path

1. Look up curve in HW map.
2. If curve found: attempt HW verify.
3. If HW returns success → accept.
4. If HW returns any error (including VERFAIL) → **fall through to SW verify**.
5. If curve not in map → SW verify directly.

**Benefits:**
- P-256 verification is hardware-accelerated (common in TLS certificate
  chain validation).
- SW fallback on HW VERFAIL catches hardware false negatives. This was
  observed with the PUF SE's ECC engine when the message digest exceeds
  `DLEN_MAX` (64 bytes) in the kernel — the digest is silently truncated,
  causing verification to fail against signatures produced with the full
  digest.

**Limitations:**
- The double-check (HW then SW on failure) adds latency when HW fails.
  For curves where HW always fails, this is wasted work. In practice this
  only affects edge cases with non-standard digest lengths.

---

### ECDH (`MBEDTLS_ECDH_GEN_PUBLIC_ALT`, `MBEDTLS_ECDH_COMPUTE_SHARED_ALT`)

#### Supported Curves

Same as ECDSA: P-192, P-224, P-256, P-384, P-521.

#### gen_public Path

1. Look up curve. If found: generate ephemeral private key in HW PRK_0
   slot, derive public key in HW.
2. Store a sentinel value (`0x100 | slot_index`) in the MPI `d` so that
   `compute_shared` can detect the key is in HW.
3. On any HW failure: fall back to SW key generation.

#### compute_shared Path

1. If `d` contains the HW sentinel: use the PRK slot directly (no import).
2. If `d` is a regular SW key **and** qlen ≤ 32: import to PRK_0, compute
   CDH in HW, clear slot.
3. If qlen > 32 or HW fails: SW fallback.

**Benefits:**
- For the full ECDHE flow (gen_public → compute_shared), the private key
  **never leaves the hardware**. The sentinel pattern keeps it in the PRK
  slot across both calls. This is the strongest security property in the
  entire ALT layer.
- P-256 ECDHE (standard TLS 1.3 key exchange) is fully HW-accelerated
  end-to-end.

**Limitations:**
- P-384 and P-521 ECDHE: gen_public works in HW (ephemeral key generation
  bypasses KWP), but compute_shared with an externally-provided SW private
  key falls back to SW due to the KWP import limitation.
- Only one PRK slot (PRK_0) is used. Concurrent ECDH operations from
  different threads will serialize on the hardware lock.
- The sentinel encoding (`0x100 | slot`) is fragile: if application code
  inspects or modifies `d` between gen_public and compute_shared, the
  sentinel is lost and the key must be re-imported (which will fail for
  large curves).

---

## Summary: When Does Hardware Accelerate?

| Algorithm | HW Accelerated When | SW Fallback When |
|-----------|-------------------|-----------------|
| AES-CBC/CTR | `length >= 4096` | Small blocks, CFB/OFB/XTS/ECB |
| AES-CCM | Always | Never (fails if no HW) |
| AES-GCM | Always (2 ioctls one-shot, 4 streaming) | Never (fails if no HW) |
| AES-CMAC | Always (AES only) | DES3-CMAC; fails if no HW for AES |
| SHA-256 | `total_input > 256` bytes | ≤ 256 bytes; cloned contexts |
| SHA-512 | Always | Never (fails if no HW) |
| ECDSA Sign | P-192/224/256 | P-384/521; unsupported curves |
| ECDSA Verify | P-192/224/256/384/521 | HW error/false-negative; unsupported curves |
| ECDH Gen | P-192/224/256/384/521 | HW failure; unsupported curves |
| ECDH Shared | P-192/224/256 (or HW-generated key) | P-384/521 SW key import; unsupported curves |

## Security Considerations

### Benefits of HW Acceleration

1. **Constant-time execution.** The PUF SE ECC and AES engines operate in
   fixed cycles regardless of key/data values, eliminating timing
   side-channels.
2. **Hardware RNG for ECDSA/ECDH.** Ephemeral keys and ECDSA nonces use
   the PUF SE's hardware random number generator, avoiding SW PRNG
   weaknesses.
3. **Key isolation for ECDHE.** In the full ECDHE flow, the ephemeral
   private key never exists in user memory.
4. **Atomic tag verification.** CCM/GCM authentication tags are compared
   inside the hardware, eliminating timing-based tag forgery attacks.

### SW Fallback Security

The SW fallback paths use mbedtls's own implementations, which are
well-audited and include:
- Constant-time scalar multiplication for ECC (blinding, Montgomery ladder)
- `mbedtls_platform_zeroize()` for sensitive data clearing
- CTR-DRBG based PRNG for nonce generation

On the K230 (single-user RT-Smart RTOS, no multi-tenant workloads), the
practical risk from SW crypto side-channels is minimal. There is no
attacker process sharing CPU caches or monitoring power/EM emissions in
typical deployment scenarios.

### Known HW Limitations

| Issue | Impact | Mitigation |
|-------|--------|-----------|
| KWP import fails for keys > 256 bits | P-384/521 ECDSA sign and ECDH shared secret fall back to SW | Early `qlen > 32` check skips HW; no error log spam |
| KWP plaintext export blocked (status 0x2) | Some key slots refuse plaintext export at HW level | Only affects key export; import + use works fine |
| `DLEN_MAX` = 64 in kernel truncates digests | Verify with digest > 64 bytes produces false negative | SW fallback catches and re-verifies |
| `SW_KEY_MAXLEN` = 64 in kernel truncates P-521 keys | Would corrupt 66-byte P-521 private key on import | Prevented by `qlen > 32` guard |
| Single PRK_0 slot for all ECC operations | Concurrent threads serialize on HW lock | Acceptable for typical embedded workloads |

## Benchmark

```text
msh />/sdcard/app/examples/3rd_party/mbedtls/mbedtls_benchmark

  MD5                      :     134954 KiB/s,          0 cycles/byte
  RIPEMD160                :      60173 KiB/s,          0 cycles/byte
  SHA-1                    :      68095 KiB/s,          0 cycles/byte
  SHA-256                  :      20929 KiB/s,          1 cycles/byte
  SHA-512                  :      55754 KiB/s,          0 cycles/byte
  SHA3-224                 :      14125 KiB/s,          1 cycles/byte
  SHA3-256                 :      14100 KiB/s,          1 cycles/byte
  SHA3-384                 :      11407 KiB/s,          2 cycles/byte
  SHA3-512                 :       7707 KiB/s,          3 cycles/byte
  3DES                     :       5958 KiB/s,          4 cycles/byte
  DES                      :      14723 KiB/s,          1 cycles/byte
  3DES-CMAC                :       5602 KiB/s,          4 cycles/byte
  AES-CBC-128              :      27418 KiB/s,          0 cycles/byte
  AES-CBC-192              :      24010 KiB/s,          1 cycles/byte
  AES-CBC-256              :      21334 KiB/s,          1 cycles/byte
  AES-CFB128-128           :      26221 KiB/s,          1 cycles/byte
  AES-CFB128-192           :      23067 KiB/s,          1 cycles/byte
  AES-CFB128-256           :      20581 KiB/s,          1 cycles/byte
  AES-CFB8-128             :       1678 KiB/s,         15 cycles/byte
  AES-CFB8-192             :       1465 KiB/s,         17 cycles/byte
  AES-CFB8-256             :       1304 KiB/s,         20 cycles/byte
  AES-CTR-128              :      26048 KiB/s,          1 cycles/byte
  AES-CTR-192              :      22980 KiB/s,          1 cycles/byte
  AES-CTR-256              :      20438 KiB/s,          1 cycles/byte
  AES-XTS-128              :      28489 KiB/s,          0 cycles/byte
  AES-XTS-256              :      21740 KiB/s,          1 cycles/byte
  AES-GCM-128              :      32403 KiB/s,          0 cycles/byte
  AES-GCM-192              :      32288 KiB/s,          0 cycles/byte
  AES-GCM-256              :      32348 KiB/s,          0 cycles/byte
  AES-CCM-128              :      29669 KiB/s,          0 cycles/byte
  AES-CCM-192              :      29315 KiB/s,          0 cycles/byte
  AES-CCM-256              :      28870 KiB/s,          0 cycles/byte
  ChaCha20-Poly1305        :      21948 KiB/s,          1 cycles/byte
  AES-CMAC-128             :      43354 KiB/s,          0 cycles/byte
  AES-CMAC-192             :      42953 KiB/s,          0 cycles/byte
  AES-CMAC-256             :      42429 KiB/s,          0 cycles/byte
  AES-CMAC-PRF-128         :      43311 KiB/s,          0 cycles/byte
  ARIA-CBC-128             :      11469 KiB/s,          2 cycles/byte
  ARIA-CBC-192             :       9928 KiB/s,          2 cycles/byte
  ARIA-CBC-256             :       8767 KiB/s,          3 cycles/byte
  CAMELLIA-CBC-128         :      18320 KiB/s,          1 cycles/byte
  CAMELLIA-CBC-192         :      14593 KiB/s,          1 cycles/byte
  CAMELLIA-CBC-256         :      14559 KiB/s,          1 cycles/byte
  ChaCha20                 :      26259 KiB/s,          1 cycles/byte
  Poly1305                 :     197380 KiB/s,          0 cycles/byte
  CTR_DRBG (NOPR)          :      19909 KiB/s,          1 cycles/byte
  CTR_DRBG (PR)            :      14686 KiB/s,          1 cycles/byte
  HMAC_DRBG SHA-1 (NOPR)   :       4640 KiB/s,          5 cycles/byte
  HMAC_DRBG SHA-1 (PR)     :       4296 KiB/s,          6 cycles/byte
  HMAC_DRBG SHA-256 (NOPR) :       3263 KiB/s,          8 cycles/byte
  HMAC_DRBG SHA-256 (PR)   :       3271 KiB/s,          8 cycles/byte
  RSA-2048                 :     367  public/s
  RSA-2048                 :      13 private/s
  RSA-3072                 :     167  public/s
  RSA-3072                 :       5 private/s
  RSA-4096                 :      95  public/s
  RSA-4096                 :       2 private/s
  DHE-2048                 :       3 handshake/s
  DH-2048                  :       5 handshake/s
  DHE-3072                 :       1 handshake/s
  DH-3072                  :       1 handshake/s
  ECDSA-secp521r1          :      93 sign/s
  ECDSA-brainpoolP512r1    :      12 sign/s
  ECDSA-secp384r1          :     157 sign/s
  ECDSA-brainpoolP384r1    :      26 sign/s
  ECDSA-secp256r1          :      51 sign/s
  ECDSA-secp256k1          :     232 sign/s
  ECDSA-brainpoolP256r1    :      48 sign/s
  ECDSA-secp224r1          :      80 sign/s
  ECDSA-secp224k1          :     267 sign/s
  ECDSA-secp192r1          :      98 sign/s
  ECDSA-secp192k1          :     359 sign/s
  ECDSA-secp521r1          :       7 verify/s
  ECDSA-brainpoolP512r1    :       3 verify/s
  ECDSA-secp384r1          :      20 verify/s
  ECDSA-brainpoolP384r1    :       6 verify/s
  ECDSA-secp256r1          :      51 verify/s
  ECDSA-secp256k1          :      72 verify/s
  ECDSA-brainpoolP256r1    :      12 verify/s
  ECDSA-secp224r1          :      81 verify/s
  ECDSA-secp224k1          :      82 verify/s
  ECDSA-secp192r1          :     103 verify/s
  ECDSA-secp192k1          :     102 verify/s
  ECDHE-secp521r1          :      25 ephemeral handshake/s
  ECDHE-brainpoolP512r1    :       3 ephemeral handshake/s
  ECDHE-secp384r1          :      42 ephemeral handshake/s
  ECDHE-brainpoolP384r1    :       6 ephemeral handshake/s
  ECDHE-secp256r1          :      30 ephemeral handshake/s
  ECDHE-secp256k1          :      75 ephemeral handshake/s
  ECDHE-brainpoolP256r1    :      12 ephemeral handshake/s
  ECDHE-secp224r1          :      43 ephemeral handshake/s
  ECDHE-secp224k1          :      84 ephemeral handshake/s
  ECDHE-secp192r1          :      72 ephemeral handshake/s
  ECDHE-secp192k1          :     112 ephemeral handshake/s
  ECDHE-x25519             :      64 ephemeral handshake/s
  ECDHE-x448               :      23 ephemeral handshake/s
  ECDH-secp521r1           :      34 static handshake/s
  ECDH-brainpoolP512r1     :       4 static handshake/s
  ECDH-secp384r1           :      57 static handshake/s
  ECDH-brainpoolP384r1     :       7 static handshake/s
  ECDH-secp256r1           :      67 static handshake/s
  ECDH-secp256k1           :     103 static handshake/s
  ECDH-brainpoolP256r1     :      16 static handshake/s
  ECDH-secp224r1           :      85 static handshake/s
  ECDH-secp224k1           :     116 static handshake/s
  ECDH-secp192r1           :     229 static handshake/s
  ECDH-secp192k1           :     152 static handshake/s
  ECDH-x25519              :     129 static handshake/s
  ECDH-x448                :      45 static handshake/s

```
