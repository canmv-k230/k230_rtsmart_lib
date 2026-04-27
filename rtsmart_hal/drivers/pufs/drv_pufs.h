/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __DRV_PUFS_HAL_H__
#define __DRV_PUFS_HAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/ioctl.h>

/* ===== Constants (mirror kernel drv_pufs.h) ===== */
#define PUFS_HMAC_BLOCK_MAXLEN  128
#define PUFS_SW_KEY_MAXLEN      64
#define PUFS_DGST_INT_STATE_LEN 64
#define PUFS_BC_BLOCK_SIZE      16
#define PUFS_DGST_MAX_LEN       64
#define PUFS_CMAC_BLOCK_SIZE    16
#define PUFS_QLEN_MAX           72
#define PUFS_NLEN_MAX           72
#define PUFS_ERR_VERFAIL        (-8)

/* ===== Operation types ===== */
enum pufs_op_type {
    PUFS_OP_INIT = 0,
    PUFS_OP_UPDATE,
    PUFS_OP_FINAL,
    PUFS_OP_INIT_UPDATE,   /* init + first update in one call */
    PUFS_OP_UPDATE_FINAL,  /* last update + final in one call */
};

/* ===== Ioctl commands (mirror kernel) ===== */
#define PUFS_UID_GET        _IOWR('P', 0x00, int)
#define PUFS_OTP_READ       _IOWR('P', 0x01, int)
#define PUFS_OTP_WRITE      _IOWR('P', 0x02, int)
#define PUFS_OTP_LOCK       _IOWR('P', 0x03, int)
#define PUFS_RNG_READ       _IOWR('P', 0x04, int)
#define PUFS_OTP_RWLCK_GET  _IOWR('P', 0x05, int)
#define PUFS_RT_VERSION     _IOWR('P', 0x06, int)
#define PUFS_KEY2OTP        _IOWR('P', 0x07, int)
#define PUFS_OTP_SEC_CFG    _IOWR('P', 0x0A, int)
#define PUFS_OTP_SEC_LOCK   _IOWR('P', 0x0B, int)
#define PUFS_OTP_SEC_STATE  _IOWR('P', 0x0C, int)
#define PUFS_KEY_INOUT      _IOWR('P', 0x10, int)
#define PUFS_KEY_DERIVE     _IOWR('P', 0x11, int)
#define PUFS_ECC_PRK_GEN    _IOWR('P', 0x40, int)
#define PUFS_ECC_PUK_GEN    _IOWR('P', 0x41, int)
#define PUFS_ECC_PUK_VERIFY _IOWR('P', 0x42, int)
#define PUFS_ECC_CDH        _IOWR('P', 0x43, int)
#define PUFS_ECDSA_SIGN     _IOWR('P', 0x45, int)
#define PUFS_ECDSA_VERIFY   _IOWR('P', 0x46, int)
#define PUFS_SM2_SIGN       _IOWR('P', 0x48, int)
#define PUFS_SM2_VERIFY     _IOWR('P', 0x49, int)
#define PUFS_SM2_ENC        _IOWR('P', 0x4A, int)
#define PUFS_SM2_DEC        _IOWR('P', 0x4B, int)
#define PUFS_SM2_KEX        _IOWR('P', 0x4C, int)
#define PUFS_RSA_SIGN       _IOWR('P', 0x4E, int)
#define PUFS_RSA_VERIFY     _IOWR('P', 0x4F, int)
#define PUFS_HASH_OP        _IOWR('P', 0x60, int)
#define PUFS_HMAC_OP        _IOWR('P', 0x61, int)
#define PUFS_CMAC_OP        _IOWR('P', 0x62, int)
#define PUFS_SP38A_OP       _IOWR('P', 0x63, int)
#define PUFS_SP38D_OP       _IOWR('P', 0x64, int)
#define PUFS_SP38C_OP       _IOWR('P', 0x65, int)
#define PUFS_SP38E_OP       _IOWR('P', 0x66, int)
#define PUFS_DRBG_INIT      _IOWR('P', 0x70, int)
#define PUFS_DRBG_RESEED    _IOWR('P', 0x71, int)
#define PUFS_DRBG_GENERATE  _IOWR('P', 0x72, int)
#define PUFS_DRBG_UNINIT    _IOWR('P', 0x73, int)

/* ===== Enums ===== */
typedef enum {
    KT_SWKEY, KT_OTPKEY, KT_PUFKEY, KT_RANDKEY,
    KT_SHARESEC, KT_SSKEY, KT_PRKEY,
} pufs_keytype_t;

typedef enum {
    HASH_SHA_224, HASH_SHA_256, HASH_SHA_384, HASH_SHA_512,
    HASH_SHA_512_224, HASH_SHA_512_256, HASH_SM3,
} pufs_hashtype_t;

typedef enum {
    ECC_NISTB163, ECC_NISTB233, ECC_NISTB283, ECC_NISTB409, ECC_NISTB571,
    ECC_NISTK163, ECC_NISTK233, ECC_NISTK283, ECC_NISTK409, ECC_NISTK571,
    ECC_NISTP192, ECC_NISTP224, ECC_NISTP256, ECC_NISTP384, ECC_NISTP521,
    ECC_SM2,
} pufs_ecctype_t;

typedef enum {
    RSA_1024, RSA_2048, RSA_3072, RSA_4096,
} pufs_rsatype_t;

typedef enum {
    RSA_BASE, RSA_X931, RSA_P1V15, RSA_PSS,
} pufs_rsamode_t;

typedef enum {
    SM2CT_C1C2C3, SM2CT_C1C3C2,
} pufs_sm2ct_format_t;

typedef enum {
    MAC_HMAC, MAC_CMAC,
} pufs_mac_cipher_t;

/* Key slot identifiers */
typedef enum {
    KS_SK128_0, KS_SK128_1, KS_SK128_2, KS_SK128_3,
    KS_SK128_4, KS_SK128_5, KS_SK128_6, KS_SK128_7,
    KS_SK256_0, KS_SK256_1, KS_SK256_2, KS_SK256_3,
    KS_SK512_0, KS_SK512_1,
    KS_PRK_0, KS_PRK_1, KS_PRK_2,
    KS_SHARESEC_0,
} pufs_keyslot_t;

/* Key wrap modes */
typedef enum {
    KW_AES_CBC_CS2, KW_AES_KW, KW_AES_KWP, KW_AES_KW_INV, KW_AES_KWP_INV,
} pufs_keywrap_t;

/* Key I/O modes */
typedef enum {
    KM_IMPORT_PT, KM_IMPORT_WRAP, KM_EXPORT_PT, KM_EXPORT_WRAP, KM_CLEAR,
} pufs_keymode_t;

/* DRBG types */
typedef enum {
    DRBG_AES_CTR, DRBG_HASH, DRBG_HMAC,
} pufs_drbg_type_t;

/* PUFrt slot identifiers */
typedef enum {
    PUFSLOT_0, PUFSLOT_1, PUFSLOT_2, PUFSLOT_3,
    OTPKEY_0,  OTPKEY_1,  OTPKEY_2,  OTPKEY_3,
    OTPKEY_4,  OTPKEY_5,  OTPKEY_6,  OTPKEY_7,
    OTPKEY_8,  OTPKEY_9,  OTPKEY_10, OTPKEY_11,
    OTPKEY_12, OTPKEY_13, OTPKEY_14, OTPKEY_15,
    OTPKEY_16, OTPKEY_17, OTPKEY_18, OTPKEY_19,
    OTPKEY_20, OTPKEY_21, OTPKEY_22, OTPKEY_23,
    OTPKEY_24, OTPKEY_25, OTPKEY_26, OTPKEY_27,
    OTPKEY_28, OTPKEY_29, OTPKEY_30, OTPKEY_31,
} pufs_rt_slot_t;

/* OTP lock states */
typedef enum {
    OTP_NA,  /* No-Access */
    OTP_RO,  /* Read-Only */
    OTP_RW,  /* Read-Write */
    OTP_SKIP, /* Skip lock setting (for key_to_otp) */
} pufs_otp_lock_state_t;

/* Key derivation methods */
typedef enum {
    KD_METHOD_PBKDF, KD_METHOD_KBKDF_EXPAND, KD_METHOD_KBKDF_EXTRACT,
    KD_METHOD_KBKDF_EXPAND_EXTRACT, KD_METHOD_SM2,
} pufs_kd_md_t;

/* Key derivation PRF types */
typedef enum {
    KD_PRF_HMAC, KD_PRF_HASH, KD_PRF_CMAC,
} pufs_kd_prf_t;

typedef enum {
    SK_AES, SK_SM4,
} pufs_skcipher_t;

typedef enum {
    MODE_ECB = 1, MODE_CFB, MODE_OFB,
    MODE_CBC, MODE_CBC_CS1, MODE_CBC_CS2, MODE_CBC_CS3,
    MODE_CTR_32, MODE_CTR_64, MODE_CTR,
    MODE_GCM = 0x10, MODE_CCM, MODE_XTS,
} pufs_skcipher_mode_t;

/* ===== Portable context types (mirror kernel) ===== */
typedef struct {
    uint8_t buff[PUFS_HMAC_BLOCK_MAXLEN];
    uint8_t key[PUFS_HMAC_BLOCK_MAXLEN];
    uint8_t state[PUFS_DGST_INT_STATE_LEN];
    uint32_t buflen;
    uint32_t keybits;
    uint32_t minlen;
    uint32_t curlen;
    uint32_t keyslot;
    uint32_t blocklen;
    uint32_t keytype;
    uint32_t op;
    uint32_t hash;
    bool start;
} pufs_hash_ctx_t;

typedef struct {
    uint8_t buff[PUFS_CMAC_BLOCK_SIZE];
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t state[PUFS_DGST_INT_STATE_LEN];
    uint32_t buflen;
    uint32_t keybits;
    uint32_t minlen;
    uint32_t keyslot;
    uint32_t keytype;
    uint32_t op;
    uint32_t cipher;
    bool start;
} pufs_cmac_ctx_t;

typedef struct {
    uint8_t buff[2 * PUFS_BC_BLOCK_SIZE];
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t iv[PUFS_BC_BLOCK_SIZE];
    uint32_t buflen;
    uint32_t keybits;
    uint32_t minlen;
    uint32_t keyslot;
    uint32_t keytype;
    uint32_t op;
    uint32_t cipher;
    bool encrypt;
    bool start;
} pufs_sp38a_ctx_t;

typedef struct {
    uint64_t aadlen;
    uint64_t inlen;
    uint64_t currentlen;
    uint8_t buff[PUFS_BC_BLOCK_SIZE];
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t ctri[PUFS_BC_BLOCK_SIZE];
    uint8_t cbcmac[PUFS_BC_BLOCK_SIZE];
    uint32_t qlen;
    uint32_t buflen;
    uint32_t keybits;
    uint32_t minlen;
    uint32_t keyslot;
    uint32_t taglen;
    uint32_t keytype;
    uint32_t op;
    uint32_t stage;
    uint32_t cipher;
    bool encrypt;
    bool ctr_start;
    bool cbcmac_start;
} pufs_sp38c_ctx_t;

typedef struct {
    uint64_t aadbits;
    uint64_t inbits;
    uint8_t buff[PUFS_BC_BLOCK_SIZE];
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t j0[PUFS_BC_BLOCK_SIZE];
    uint8_t ghash[PUFS_BC_BLOCK_SIZE];
    uint32_t buflen;
    uint32_t keybits;
    uint32_t minlen;
    uint32_t keyslot;
    uint32_t incj0;
    uint32_t keytype;
    uint32_t op;
    uint32_t stage;
    uint32_t cipher;
    bool encrypt;
    bool start;
} pufs_sp38d_ctx_t;

typedef struct {
    uint8_t buff[2 * PUFS_BC_BLOCK_SIZE];
    uint8_t key1[PUFS_SW_KEY_MAXLEN];
    uint8_t key2[PUFS_SW_KEY_MAXLEN];
    uint8_t i[PUFS_BC_BLOCK_SIZE];
    uint32_t buflen;
    uint32_t keybits;
    uint32_t minlen;
    uint32_t keyslot1;
    uint32_t keyslot2;
    uint32_t j;
    uint32_t keytype1;
    uint32_t keytype2;
    uint32_t op;
    uint32_t cipher;
    bool encrypt;
    bool start;
} pufs_sp38e_ctx_t;

/* ===== Ioctl payload structs (mirror kernel) ===== */
typedef struct {
    uint8_t op;
    uint8_t hash;
    uint8_t _pad[2];
    uint32_t msglen;
    uint64_t msg_phys;
    uint64_t dgst_phys;
    uint32_t dlen;
    pufs_hash_ctx_t ctx;
} pufs_hash_op_t;

typedef struct {
    uint8_t op;
    uint8_t hash;
    uint8_t keytype;
    uint8_t _pad;
    uint32_t keybits;
    uint32_t msglen;
    uint64_t msg_phys;
    uint64_t dgst_phys;
    uint32_t dlen;
    pufs_hash_ctx_t ctx;
} pufs_hmac_op_t;

typedef struct {
    uint8_t op;
    uint8_t cipher;
    uint8_t keytype;
    uint8_t _pad;
    uint32_t keybits;
    uint32_t msglen;
    uint64_t msg_phys;
    uint64_t dgst_phys;
    uint32_t dlen;
    pufs_cmac_ctx_t ctx;
} pufs_cmac_op_t;

typedef struct {
    uint8_t op;
    uint8_t cipher;
    uint8_t mode;
    uint8_t encrypt;
    uint8_t keytype;
    uint8_t _pad[3];
    uint32_t keybits;
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t iv[PUFS_BC_BLOCK_SIZE];
    uint32_t ivlen;
    uint32_t inlen;
    uint64_t in_phys;
    uint64_t out_phys;
    uint32_t outlen;
    pufs_sp38a_ctx_t ctx;
} pufs_sp38a_op_t;

typedef struct {
    uint8_t op;
    uint8_t cipher;
    uint8_t encrypt;
    uint8_t keytype;
    uint32_t keybits;
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t iv[PUFS_BC_BLOCK_SIZE];
    uint32_t ivlen;
    uint32_t inlen;
    uint64_t in_phys;
    uint64_t out_phys;
    uint32_t outlen;
    uint64_t tag_phys;
    uint32_t taglen;
    pufs_sp38d_ctx_t ctx;
} pufs_sp38d_op_t;

typedef struct {
    uint8_t op;
    uint8_t cipher;
    uint8_t encrypt;
    uint8_t keytype;
    uint32_t keybits;
    uint8_t key[PUFS_SW_KEY_MAXLEN];
    uint8_t nonce[PUFS_BC_BLOCK_SIZE];
    uint32_t noncelen;
    uint64_t ccm_aadlen;
    uint64_t ccm_inlen;
    uint32_t taglen;
    uint32_t inlen;
    uint64_t in_phys;
    uint64_t out_phys;
    uint32_t outlen;
    uint64_t tag_phys;
    pufs_sp38c_ctx_t ctx;
} pufs_sp38c_op_t;

typedef struct {
    uint8_t op;
    uint8_t cipher;
    uint8_t encrypt;
    uint8_t keytype1;
    uint8_t keytype2;
    uint8_t _pad[3];
    uint32_t keybits;
    uint8_t key1[PUFS_SW_KEY_MAXLEN];
    uint8_t key2[PUFS_SW_KEY_MAXLEN];
    uint8_t iv[PUFS_BC_BLOCK_SIZE];
    uint32_t ivlen;
    uint32_t inlen;
    uint64_t in_phys;
    uint64_t out_phys;
    uint32_t outlen;
    pufs_sp38e_ctx_t ctx;
} pufs_sp38e_op_t;

/* Atomic operation structs */
typedef struct {
    uint8_t uid[32];
} pufs_uid_t;

typedef struct {
    uint8_t slot;
    uint64_t uid_phys;
} pufs_uid_get_t;

/* OTP read/write */
typedef struct {
    uint16_t addr;
    uint32_t len;
    uint8_t* buf;
} pufs_otp_rw_t;

/* OTP lock */
typedef struct {
    uint16_t addr;
    uint32_t len;
    uint8_t  lock;    /* 0=NA, 1=RO, 2=RW */
} pufs_otp_lock_op_t;

/* RNG read */
typedef struct {
    uint32_t len;
    uint8_t* buf;
} pufs_rng_read_t;

/* OTP rwlck query */
typedef struct {
    uint16_t addr;
    uint8_t  lock;
} pufs_otp_rwlck_get_t;

/* RT version query */
typedef struct {
    uint32_t version;
    uint32_t features;
} pufs_rt_version_t;

/* Program key to OTP */
typedef struct {
    uint8_t  slot;
    uint32_t keybits;
    uint8_t* key;
    uint8_t  lock;    /* pufs_otp_lock_t: NA/RO/RW, or N_OTP_LOCK_T to skip */
} pufs_key2otp_t;

typedef struct {
    uint8_t disable_spi2axi;
    uint8_t disable_jtag;
    uint8_t force_secure_boot;
    uint8_t disable_isp;
} pufs_otp_security_cfg_t;

typedef struct {
    uint8_t disable_spi2axi;
    uint8_t disable_jtag;
    uint8_t force_secure_boot;
    uint8_t disable_isp;
    uint8_t spi2axi_word_lock;
    uint8_t jtag_word_lock;
    uint8_t boot_ctrl_word_lock;
    uint8_t reserved0;
} pufs_otp_security_state_t;

/* DRBG instantiate */
typedef struct {
    uint8_t  mode;
    uint32_t security;
    uint8_t  df;
    uint32_t noncelen;
    uint32_t pstrlen;
    uint8_t* nonce;
    uint8_t* pstr;
} pufs_drbg_init_t;

/* DRBG reseed */
typedef struct {
    uint8_t  df;
    uint32_t adinlen;
    uint8_t* adin;
} pufs_drbg_reseed_t;

/* DRBG generate */
typedef struct {
    uint32_t outbits;
    uint8_t  pr;
    uint8_t  df;
    uint32_t adinlen;
    uint8_t* adin;
    uint8_t* out;
} pufs_drbg_generate_t;

typedef struct {
    uint32_t qlen;
    uint8_t x[PUFS_QLEN_MAX];
    uint8_t y[PUFS_QLEN_MAX];
} pufs_ecc_puk_t;

typedef struct {
    uint32_t qlen;
    uint8_t r[PUFS_NLEN_MAX];
    uint8_t s[PUFS_NLEN_MAX];
} pufs_ecdsa_sig_t;

typedef struct {
    uint8_t ecctype;
    uint8_t prktype;
    uint8_t prkslot;
    pufs_ecc_puk_t* puk;
} pufs_ecc_puk_gen_t;

typedef struct {
    uint8_t ecctype;
    pufs_ecc_puk_t* puk;
} pufs_ecc_puk_verify_t;

typedef struct {
    uint8_t ecctype;
    uint8_t prktype;
    uint8_t prkslot;
    uint32_t mdlen;
    uint64_t md_phys;
    uint64_t sig_phys;
} pufs_ecdsa_sign_t;

typedef struct {
    uint8_t ecctype;
    uint32_t mdlen;
    union {
        uint64_t puk_phys;
        uint64_t otpslot;
    };
    uint64_t md_phys;
    uint64_t sig_phys;
} pufs_ecdsa_verify_t;

typedef struct {
    uint8_t prktype;
    uint8_t prkslot;
    uint32_t idlen;
    uint32_t msglen;
    uint64_t id_phys;
    uint64_t msg_phys;
    uint64_t sig_phys;
} pufs_sm2_sign_t;

typedef struct {
    uint32_t idlen;
    uint32_t msglen;
    uint64_t puk_phys;
    uint64_t id_phys;
    uint64_t msg_phys;
    uint64_t sig_phys;
} pufs_sm2_verify_t;

typedef struct {
    uint8_t format;
    uint32_t inlen;
    uint8_t* in;
    uint8_t* out;
    uint32_t* outlen;
    pufs_ecc_puk_t* puk;
} pufs_sm2_enc_t;

typedef struct {
    uint8_t format;
    uint8_t prkslot;
    uint32_t inlen;
    uint8_t* in;
    uint8_t* out;
    uint32_t* outlen;
} pufs_sm2_dec_t;

typedef struct {
    uint8_t init;
    uint8_t prkslotl;
    uint8_t tprkslotl;
    uint32_t idllen;
    uint32_t idrlen;
    uint32_t keybits;
    pufs_ecc_puk_t* pukr;
    pufs_ecc_puk_t* tpukr;
    uint8_t* idl;
    uint8_t* idr;
    uint8_t* key;
    uint8_t* dgst2;
    uint32_t* dlen2;
    uint8_t* dgst3;
    uint32_t* dlen3;
} pufs_sm2_kex_t;

/* Key I/O */
typedef struct {
    uint8_t mode;
    uint8_t keytype;
    uint8_t keyslot;
    uint8_t* keyaddr;
    uint32_t keybits;
    uint8_t keywrap;
    uint8_t kwslot;
    uint32_t kwbits;
} pufs_key_io_t;

/* Key derivation */
typedef struct {
    uint8_t keytype;
    uint8_t keyslot;
    uint8_t method;
    uint8_t prf;
    uint8_t hash;
    uint8_t feedback;
    uint8_t ztype;
    uint32_t outbits;
    uint32_t iter;
    uint32_t ctrpos;
    uint32_t ctrlen;
    uint32_t zbits;
    uint32_t saltlen;
    uint32_t infolen;
    uint8_t* iv;
    uint8_t* zaddr;
    uint8_t* salt;
    uint8_t* info;
    uint8_t* out;
} pufs_key_derive_t;

/* ECC private key generation */
typedef struct {
    uint8_t ecctype;
    uint8_t is_ephemeral;
    uint8_t prkslot;
    uint8_t keytype;
    uint8_t hashtype;
    uint32_t keybits;
    uint32_t saltlen;
    uint32_t infolen;
    uint8_t* keyaddr;
    uint8_t* salt;
    uint8_t* info;
} pufs_ecc_prk_gen_t;

/* ECC CDH (ECDH) */
typedef struct {
    uint8_t ecctype;
    uint8_t is_ephemeral;
    uint8_t prkslot_e;
    uint8_t prktype_s;
    uint8_t prkslot_s;
    pufs_ecc_puk_t* puk_e;
    pufs_ecc_puk_t* puk_s;
    uint8_t* out;
} pufs_ecc_cdh_t;

typedef struct {
    uint8_t rsamode;
    uint8_t rsatype;
    uint8_t hashtype;
    uint32_t puk;
    uint32_t msglen;
    uint32_t saltlen;
    uint64_t sig_phys;
    uint64_t n_phys;
    uint64_t prk_phys;
    uint64_t msg_phys;
    uint64_t salt_phys;
} pufs_rsa_sign_t;

typedef struct {
    uint8_t rsamode;
    uint8_t rsatype;
    uint8_t hashtype;
    uint32_t puk;
    uint32_t msglen;
    uint64_t sig_phys;
    uint64_t n_phys;
    uint64_t msg_phys;
} pufs_rsa_verify_t;

/* ===== HAL Instance types ===== */

typedef struct {
    int fd;
    uint32_t buf_size;
    pthread_mutex_t io_lock;
    int io_lock_init;
} drv_pufs_inst;

typedef struct {
    drv_pufs_inst *dev;
    pufs_hash_ctx_t ctx;
} drv_pufs_hash_inst;

typedef struct {
    drv_pufs_inst *dev;
    pufs_cmac_ctx_t ctx;
} drv_pufs_cmac_inst;

typedef struct {
    drv_pufs_inst *dev;
    uint8_t mode;   /* pufs_skcipher_mode_t */
    union {
        pufs_sp38a_ctx_t sp38a;
        pufs_sp38d_ctx_t sp38d;
        pufs_sp38c_ctx_t sp38c;
        pufs_sp38e_ctx_t sp38e;
    } ctx;
} drv_pufs_cipher_inst;

/* ===== Device management ===== */
int drv_pufs_open(drv_pufs_inst *inst);
int drv_pufs_close(drv_pufs_inst *inst);
int drv_pufs_dev_lock(drv_pufs_inst *inst);
void drv_pufs_dev_unlock(drv_pufs_inst *inst);
int drv_pufs_ioctl(int fd, unsigned long request, void *arg);

/* ===== Hash API ===== */
int drv_pufs_hash_init(drv_pufs_hash_inst *inst, drv_pufs_inst *dev, pufs_hashtype_t hash);
int drv_pufs_hash_update(drv_pufs_hash_inst *inst, const uint8_t *msg, uint32_t msglen);
int drv_pufs_hash_final(drv_pufs_hash_inst *inst, uint8_t *dgst, uint32_t *dlen);

/* ===== HMAC API ===== */
int drv_pufs_hmac_init(drv_pufs_hash_inst *inst, drv_pufs_inst *dev,
                       pufs_hashtype_t hash, pufs_keytype_t keytype,
                       const uint8_t *key, uint32_t keybits);
int drv_pufs_hmac_update(drv_pufs_hash_inst *inst, const uint8_t *msg, uint32_t msglen);
int drv_pufs_hmac_final(drv_pufs_hash_inst *inst, uint8_t *dgst, uint32_t *dlen);

/* ===== CMAC API ===== */
int drv_pufs_cmac_init(drv_pufs_cmac_inst *inst, drv_pufs_inst *dev,
                       pufs_skcipher_t cipher, pufs_keytype_t keytype,
                       const uint8_t *key, uint32_t keybits);
int drv_pufs_cmac_update(drv_pufs_cmac_inst *inst, const uint8_t *msg, uint32_t msglen);
int drv_pufs_cmac_final(drv_pufs_cmac_inst *inst, uint8_t *dgst, uint32_t *dlen);

/* ===== Symmetric cipher API ===== */
int drv_pufs_cipher_init(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                         pufs_skcipher_t cipher, pufs_skcipher_mode_t mode,
                         int encrypt, pufs_keytype_t keytype,
                         const uint8_t *key, uint32_t keybits,
                         const uint8_t *iv, uint32_t ivlen);
/* CCM-specific init (needs extra lengths) */
int drv_pufs_cipher_ccm_init(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                             pufs_skcipher_t cipher, int encrypt,
                             pufs_keytype_t keytype,
                             const uint8_t *key, uint32_t keybits,
                             const uint8_t *nonce, uint32_t noncelen,
                             uint64_t aadlen, uint64_t inlen, uint32_t taglen);
/* XTS-specific init (two keys) */
int drv_pufs_cipher_xts_init(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                             pufs_skcipher_t cipher, int encrypt,
                             pufs_keytype_t keytype1, const uint8_t *key1,
                             pufs_keytype_t keytype2, const uint8_t *key2,
                             uint32_t keybits,
                             const uint8_t *iv, uint32_t ivlen);
int drv_pufs_cipher_update(drv_pufs_cipher_inst *inst,
                           uint8_t *out, uint32_t *outlen,
                           const uint8_t *in, uint32_t inlen);
int drv_pufs_cipher_final(drv_pufs_cipher_inst *inst,
                          uint8_t *out, uint32_t *outlen,
                          uint8_t *tag, uint32_t taglen);

/* Compound ops: init+update and update+final in single ioctl (GCM only) */
int drv_pufs_cipher_init_update(drv_pufs_cipher_inst *inst, drv_pufs_inst *dev,
                                pufs_skcipher_t cipher, pufs_skcipher_mode_t mode,
                                int encrypt, pufs_keytype_t keytype,
                                const uint8_t *key, uint32_t keybits,
                                const uint8_t *iv, uint32_t ivlen,
                                uint8_t *out, uint32_t *outlen,
                                const uint8_t *in, uint32_t inlen);
int drv_pufs_cipher_update_final(drv_pufs_cipher_inst *inst,
                                 uint8_t *out, uint32_t *outlen,
                                 const uint8_t *in, uint32_t inlen,
                                 uint8_t *tag, uint32_t taglen);

/* ===== Asymmetric / misc API ===== */
int drv_pufs_uid_get(drv_pufs_inst *dev, uint8_t slot, uint8_t uid[32]);

int drv_pufs_ecdsa_sign(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                        uint8_t prktype, uint8_t prkslot,
                        const uint8_t *md, uint32_t mdlen,
                        pufs_ecdsa_sig_t *sig);
int drv_pufs_ecdsa_verify(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                          const uint8_t *md, uint32_t mdlen,
                          const pufs_ecc_puk_t *puk,
                          const pufs_ecdsa_sig_t *sig);

int drv_pufs_rsa_sign(drv_pufs_inst *dev, pufs_rsamode_t mode,
                      pufs_rsatype_t rsatype, pufs_hashtype_t hashtype,
                      uint32_t puk, const uint8_t *n, const uint8_t *prk,
                      const uint8_t *msg, uint32_t msglen,
                      const uint8_t *salt, uint32_t saltlen,
                      uint8_t *sig);
int drv_pufs_rsa_verify(drv_pufs_inst *dev, pufs_rsamode_t mode,
                        pufs_rsatype_t rsatype, pufs_hashtype_t hashtype,
                        uint32_t puk, const uint8_t *n,
                        const uint8_t *msg, uint32_t msglen,
                        const uint8_t *sig);

int drv_pufs_sm2_sign(drv_pufs_inst *dev, uint8_t prktype, uint8_t prkslot,
                      const uint8_t *id, uint32_t idlen,
                      const uint8_t *msg, uint32_t msglen,
                      pufs_ecdsa_sig_t *sig);
int drv_pufs_sm2_verify(drv_pufs_inst *dev,
                        const pufs_ecc_puk_t *puk,
                        const uint8_t *id, uint32_t idlen,
                        const uint8_t *msg, uint32_t msglen,
                        const pufs_ecdsa_sig_t *sig);

/* ===== ECC key management API ===== */
int drv_pufs_ecc_prk_gen(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                         int is_ephemeral, uint8_t prkslot);
int drv_pufs_ecc_puk_gen(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                         uint8_t prktype, uint8_t prkslot,
                         pufs_ecc_puk_t *puk);
int drv_pufs_ecc_puk_verify(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                            const pufs_ecc_puk_t *puk);
int drv_pufs_ecc_cdh(drv_pufs_inst *dev, pufs_ecctype_t ecctype,
                     int is_ephemeral, uint8_t prkslot_e,
                     const pufs_ecc_puk_t *puk_e,
                     uint8_t *out, uint32_t outlen);

/* ===== SM2 encryption/key exchange API ===== */
int drv_pufs_sm2_enc(drv_pufs_inst *dev, pufs_sm2ct_format_t format,
                     const uint8_t *in, uint32_t inlen,
                     uint8_t *out, uint32_t *outlen,
                     const pufs_ecc_puk_t *puk);
int drv_pufs_sm2_dec(drv_pufs_inst *dev, pufs_sm2ct_format_t format,
                     uint8_t prkslot,
                     const uint8_t *in, uint32_t inlen,
                     uint8_t *out, uint32_t *outlen);
int drv_pufs_sm2_kex(drv_pufs_inst *dev, int init,
                     uint8_t prkslotl, uint8_t tprkslotl,
                     const pufs_ecc_puk_t *pukr, const pufs_ecc_puk_t *tpukr,
                     const uint8_t *idl, uint32_t idllen,
                     const uint8_t *idr, uint32_t idrlen,
                     uint8_t *key, uint32_t keybits,
                     uint8_t *dgst2, uint32_t *dlen2,
                     uint8_t *dgst3, uint32_t *dlen3);

/* ===== Key management API ===== */
int drv_pufs_key_import_plaintext(drv_pufs_inst *dev,
                                  pufs_keytype_t keytype, uint8_t keyslot,
                                  const uint8_t *key, uint32_t keybits);
int drv_pufs_key_clear(drv_pufs_inst *dev,
                       pufs_keytype_t keytype, uint8_t keyslot,
                       uint32_t keybits);

/* ===== Key derivation API ===== */
int drv_pufs_key_derive(drv_pufs_inst *dev,
                        pufs_keytype_t keytype, uint8_t keyslot,
                        pufs_kd_md_t method, pufs_kd_prf_t prf,
                        pufs_hashtype_t hash, int feedback,
                        pufs_keytype_t ztype, const uint8_t *zaddr, uint32_t zbits,
                        const uint8_t *salt, uint32_t saltlen,
                        const uint8_t *info, uint32_t infolen,
                        const uint8_t *iv, uint32_t outbits,
                        uint32_t iter, uint32_t ctrpos, uint32_t ctrlen,
                        uint8_t *out);

/* ===== OTP API ===== */
int drv_pufs_otp_read(drv_pufs_inst *dev, uint16_t addr, uint8_t *buf, uint32_t len);
int drv_pufs_otp_write(drv_pufs_inst *dev, uint16_t addr, const uint8_t *buf, uint32_t len);
int drv_pufs_otp_lock(drv_pufs_inst *dev, uint16_t addr, uint32_t len, uint8_t lock);
int drv_pufs_otp_get_rwlck(drv_pufs_inst *dev, uint16_t addr, uint8_t *lock);
int drv_pufs_key_to_otp(drv_pufs_inst *dev, pufs_rt_slot_t slot,
                        const uint8_t *key, uint32_t keybits,
                        uint8_t lock);
int drv_pufs_otp_apply_security_config(drv_pufs_inst *dev,
                                       bool disable_spi2axi,
                                       bool disable_jtag,
                                       bool force_secure_boot,
                                       bool disable_isp);
int drv_pufs_otp_get_security_config_state(drv_pufs_inst *dev,
                                           pufs_otp_security_state_t *state);
int drv_pufs_otp_lock_security_config_words(drv_pufs_inst *dev);

/* ===== PUFrt management API ===== */
int drv_pufs_rt_version(drv_pufs_inst *dev, uint32_t *version, uint32_t *features);

/* ===== RNG API ===== */
int drv_pufs_rng_read(drv_pufs_inst *dev, uint8_t *buf, uint32_t len);

/* ===== Key export API ===== */
int drv_pufs_key_export_plaintext(drv_pufs_inst *dev,
                                  pufs_keytype_t keytype, uint8_t keyslot,
                                  uint8_t *key, uint32_t keybits);

/* ===== DRBG (SP800-90A) API ===== */
int drv_pufs_drbg_instantiate(drv_pufs_inst *dev, pufs_drbg_type_t mode,
                              uint32_t security, int df,
                              const uint8_t *nonce, uint32_t noncelen,
                              const uint8_t *pstr, uint32_t pstrlen);
int drv_pufs_drbg_reseed(drv_pufs_inst *dev, int df,
                         const uint8_t *adin, uint32_t adinlen);
int drv_pufs_drbg_generate(drv_pufs_inst *dev, uint8_t *out, uint32_t outbits,
                           int pr, int df,
                           const uint8_t *adin, uint32_t adinlen);
int drv_pufs_drbg_uninstantiate(drv_pufs_inst *dev);

#endif /* __DRV_PUFS_HAL_H__ */
