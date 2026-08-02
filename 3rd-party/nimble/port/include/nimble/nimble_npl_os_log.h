/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RTSMART_NIMBLE_NPL_OS_LOG_H
#define RTSMART_NIMBLE_NPL_OS_LOG_H

#include <stdarg.h>
#include <stdio.h>

#define BLE_NPL_LOG_IMPL(level)                                             \
    static inline void _BLE_NPL_LOG_CAT(                                    \
        BLE_NPL_LOG_MODULE, _BLE_NPL_LOG_CAT(_, level))(const char *fmt, ...) \
    {                                                                       \
        va_list args;                                                       \
        va_start(args, fmt);                                                \
        vprintf(fmt, args);                                                 \
        va_end(args);                                                       \
    }

#endif /* RTSMART_NIMBLE_NPL_OS_LOG_H */
