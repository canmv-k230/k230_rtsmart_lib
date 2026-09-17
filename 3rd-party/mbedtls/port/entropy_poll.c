#include "mbedtls/build_info.h"

#if defined(MBEDTLS_ENTROPY_C) && defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
#include <stdint.h>
#include "mbedtls/entropy.h"
#include "drv_pufs.h"

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len,
                          size_t* olen)
{
    drv_pufs_inst dev;
    (void)data;
    if (olen == NULL) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    *olen = 0;
    if (len == 0) {
        return 0;
    }
    if (output == NULL || len > UINT32_MAX || drv_pufs_open(&dev) != 0) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    int ret = drv_pufs_rng_read(&dev, output, (uint32_t)len);
    drv_pufs_close(&dev);
    if (ret != 0) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    *olen = len;
    return 0;
}
#endif
