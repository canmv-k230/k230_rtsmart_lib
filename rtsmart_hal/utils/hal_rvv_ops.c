/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "hal_rvv_ops.h"

#include <stdint.h>
#include <string.h>

void *hal_rvv_memcpy(void *dst, const void *src, size_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    size_t remaining = n;

    if (64 >= n) {
        return memcpy(dst, src, n);
    }

    while (remaining > 0) {
        size_t vl;

        asm volatile(
            ".option push\n"
            ".option arch, +v\n"
            "vsetvli %0, %1, e8, m8, ta, ma\n"
            "vle8.v v8, (%2)\n"
            "vse8.v v8, (%3)\n"
            ".option pop\n"
            : "=&r"(vl)
            : "r"(remaining), "r"(s), "r"(d)
            : "v8", "memory");

        s += vl;
        d += vl;
        remaining -= vl;
    }

    return dst;
}

void *hal_rvv_memset(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    size_t remaining = n;
    uintptr_t fill = (uint8_t)value;

    if (64 >= n) {
        return memset(dst, value, n);
    }

    while (remaining > 0) {
        size_t vl;

        asm volatile(
            ".option push\n"
            ".option arch, +v\n"
            "vsetvli %0, %1, e8, m8, ta, ma\n"
            "vmv.v.x v8, %2\n"
            "vse8.v v8, (%3)\n"
            ".option pop\n"
            : "=&r"(vl)
            : "r"(remaining), "r"(fill), "r"(d)
            : "v8", "memory");

        d += vl;
        remaining -= vl;
    }

    return dst;
}
