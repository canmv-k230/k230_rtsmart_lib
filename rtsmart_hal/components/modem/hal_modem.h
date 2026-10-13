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

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int         req_ready_gpio;   /* REQ_READY output pin; default 26 */
    int         wake_timeout_ms;  /* budget to reach online, ms; default 60000 */
    const char *at_dev;           /* AT port node; default "/dev/ttyUSB2" */
    const char *dial_cmd;         /* dial command; default "AT+MDIALUP=1,1" */
    int         dial_timeout_ms;  /* dial response timeout, ms; default 30000 */
} modem_cfg_t;

#define MODEM_IMEI_LEN 15

/* cfg: config, or NULL / zeroed fields for defaults. Returns 0 on success, <0 on failure. */
int modem_init(const modem_cfg_t *cfg);

void modem_deinit(void);

/* This boot's wake-up source name (the exact PMU Kconfig string, e.g.
 * "4G_WAKEUP"), or NULL if none was captured / not initialized. The pointer is
 * valid until modem_deinit(); the caller interprets the name. */
const char *board_get_wakeup_source(void);

/* Returns 0 when online (sockets usable), <0 on failure. */
int modem_wake(void);

/* buf: receives the 15-digit IMEI + NUL. size: at least MODEM_IMEI_LEN + 1. Returns 0 / <0. */
int modem_get_imei(char *buf, size_t size);

/* Drives REQ_READY low then powers off; no return on success, <0 if shutdown failed. */
int modem_enter_edrx(void);

#ifdef __cplusplus
}
#endif
