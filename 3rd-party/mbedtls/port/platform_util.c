/*
 * Common and shared functions used by multiple modules in the Mbed TLS
 * library.
 *
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "mbedtls/build_info.h"

#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"

#include "hal_utils.h"

#include <stdio.h>
#include <stdarg.h>

/*
 * C99-conforming snprintf wrapper.
 *
 * The toolchain's musl vsnprintf has an off-by-one: it initialises
 * cookie.n = n instead of n-1, so sn_write copies up to n bytes of
 * content and then writes a NUL terminator at position n — one byte
 * past the caller's buffer.  We compensate by passing n-1 to
 * vsnprintf (for n >= 2), handling n <= 1 as special cases.
 */
int mbedtls_platform_snprintf(char *s, size_t n, const char *fmt, ...)
{
    int ret;
    va_list ap;

    va_start(ap, fmt);
    if (n <= 1) {
        char dummy;
        ret = vsnprintf(&dummy, 0, fmt, ap);
        if (n == 1) {
            s[0] = '\0';
        }
    } else {
        ret = vsnprintf(s, n - 1, fmt, ap);
    }
    va_end(ap);
    return ret;
}

#if defined(MBEDTLS_HAVE_TIME)

#if defined(MBEDTLS_PLATFORM_MS_TIME_ALT)

mbedtls_ms_time_t mbedtls_ms_time(void) { return utils_cpu_ticks_ms(); }

#endif /* MBEDTLS_PLATFORM_MS_TIME_ALT */

#if defined(MBEDTLS_PLATFORM_TIME_ALT)

#include "canmv_misc.h"

mbedtls_time_t mbedtls_time_wrap(mbedtls_time_t* timer)
{
    mbedtls_time_t tm;

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_UTC_TIMESTAMP, &tm)) {
        tm = 0;
        mbedtls_printf("rtc get timestamp failed\n");
    }

    if (timer) {
        *timer = tm;
    }

    return tm;
}
#endif /* MBEDTLS_PLATFORM_TIME_ALT */

#endif /* MBEDTLS_HAVE_TIME */
