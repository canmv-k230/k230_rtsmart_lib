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

#include "hal_modem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "drv_fpioa.h"
#include "drv_gpio.h"
#include "drv_pmu.h"
#include "hal_netmgmt.h"

#define MODEM_LOG(fmt, ...) printf("[modem] " fmt "\n", ##__VA_ARGS__)

#define MODEM_DEF_REQ_READY_GPIO  26
#define MODEM_DEF_WAKE_TIMEOUT_MS 60000
#define MODEM_DEF_AT_DEV          "/dev/ttyUSB2"
#define MODEM_DEF_DIAL_CMD        "AT+MDIALUP=1,1"
#define MODEM_DEF_DIAL_TIMEOUT_MS 30000
#define MODEM_POLL_MS             10
#define MODEM_WAKE_SRC_LEN        64   /* PMU wake-source name buffer */

static struct {
    int              inited;      /* modem_init() done */
    modem_cfg_t      cfg;         /* effective config (defaults + overrides) */
    drv_gpio_inst_t *req_ready;   /* REQ_READY output pin handle */
    drv_pmu_inst_t  *pmu;         /* PMU handle (shutdown / wake source) */
    char             wake_src[MODEM_WAKE_SRC_LEN]; /* wake-source name, "" if none */
    char             imei[MODEM_IMEI_LEN + 1];     /* cached IMEI string */
    int              imei_valid;  /* IMEI cached */
} g_modem;

static long now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void sleep_ms(int ms)
{
    if (ms > 0)
        usleep((useconds_t)ms * 1000);
}

static void req_ready_set(int high)
{
    if (g_modem.req_ready)
        drv_gpio_value_set(g_modem.req_ready,
                           high ? GPIO_PV_HIGH : GPIO_PV_LOW);
}

static int at_open(void)
{
    int fd = open(g_modem.cfg.at_dev, O_RDWR);

    if (fd < 0)
        return -1;

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

static void at_drain(int fd)
{
    char buf[128];

    while (read(fd, buf, sizeof(buf)) > 0)
        ;
}

static int at_cmd(int fd, const char *cmd, char *resp, size_t resp_size,
                  int timeout_ms)
{
    char   line[64];
    char   local[512];
    size_t len = 0;
    long   deadline = now_ms() + timeout_ms;
    int    n;

    if (!resp || resp_size == 0) {
        resp = local;
        resp_size = sizeof(local);
    }
    resp[0] = '\0';

    at_drain(fd);

    n = snprintf(line, sizeof(line), "%s\r", cmd);
    if (n <= 0 || write(fd, line, (size_t)n) != n) {
        MODEM_LOG("write '%s' failed: errno=%d", cmd, errno);
        return -1;
    }

    for (;;) {
        char chunk[128];

        n = (int)read(fd, chunk, sizeof(chunk));
        if (n > 0) {
            size_t copy = (size_t)n;

            if (copy > resp_size - 1)
                copy = resp_size - 1;
            if (len + copy > resp_size - 1) {
                size_t drop = len + copy - (resp_size - 1);
                memmove(resp, resp + drop, len - drop);
                len -= drop;
            }
            memcpy(resp + len, chunk + (n - copy), copy);
            len += copy;
            resp[len] = '\0';

            if (strstr(resp, "ERROR"))
                return 0;
            if (strstr(resp, "OK\r") || strstr(resp, "OK\n"))
                return 1;
        }

        if (now_ms() >= deadline)
            return -1;
        sleep_ms(20);
    }
}

static int at_ready(void)
{
    int fd = at_open();
    int rc;

    if (fd < 0)
        return 0;
    rc = at_cmd(fd, "AT", NULL, 0, 500);
    close(fd);
    return rc == 1;
}

static int net_is_online(void)
{
    char name[32] = { 0 };

    if (netmgmt_utils_get_default_dev(name) != 0)
        return 0;
    return strncmp(name, "eth", 3) == 0;
}

static int modem_dial(void)
{
    char resp[256];
    int  fd = at_open();
    int  rc;

    if (fd < 0)
        return -1;

    rc = at_cmd(fd, g_modem.cfg.dial_cmd, resp, sizeof(resp),
                g_modem.cfg.dial_timeout_ms);
    close(fd);

    if (rc == 1)
        MODEM_LOG("dial '%s' -> OK", g_modem.cfg.dial_cmd);
    return rc; /* 1=OK, 0=ERROR (not ready, retry), -1=I/O error */
}

static int wait_until(int (*pred)(void), long deadline)
{
    while (!pred()) {
        if (now_ms() >= deadline)
            return -1;
        sleep_ms(MODEM_POLL_MS);
    }
    return 0;
}

static int dial_ready(void)
{
    return modem_dial() == 1 || net_is_online();
}

static int wake_attempt(void)
{
    long deadline = now_ms() + g_modem.cfg.wake_timeout_ms;

    req_ready_set(1);

    if (wait_until(at_ready, deadline) < 0) {
        MODEM_LOG("timeout waiting for AT channel %s", g_modem.cfg.at_dev);
        return -1;
    }

    if (wait_until(dial_ready, deadline) < 0) {
        MODEM_LOG("timeout: dial not accepted");
        return -1;
    }

    if (netmgmt_utils_renew_dhcp(RT_NET_DEV_LAN) != 0)
        MODEM_LOG("dhcp renew request failed (continuing)");

    if (wait_until(net_is_online, deadline) < 0) {
        MODEM_LOG("timeout: modem NIC did not come online");
        return -1;
    }
    MODEM_LOG("online");
    return 0;
}

static void apply_defaults(const modem_cfg_t *cfg)
{
    modem_cfg_t *d = &g_modem.cfg;

    d->req_ready_gpio  = MODEM_DEF_REQ_READY_GPIO;
    d->wake_timeout_ms = MODEM_DEF_WAKE_TIMEOUT_MS;
    d->at_dev          = MODEM_DEF_AT_DEV;
    d->dial_cmd        = MODEM_DEF_DIAL_CMD;
    d->dial_timeout_ms = MODEM_DEF_DIAL_TIMEOUT_MS;

    if (!cfg)
        return;

    if (cfg->req_ready_gpio > 0)  d->req_ready_gpio  = cfg->req_ready_gpio;
    if (cfg->wake_timeout_ms > 0) d->wake_timeout_ms = cfg->wake_timeout_ms;
    if (cfg->at_dev)              d->at_dev          = cfg->at_dev;
    if (cfg->dial_cmd)            d->dial_cmd        = cfg->dial_cmd;
    if (cfg->dial_timeout_ms > 0) d->dial_timeout_ms = cfg->dial_timeout_ms;
}

static void latch_wakeup_source(void)
{
    g_modem.wake_src[0] = '\0';
    if (!g_modem.pmu)
        return;

    if (drv_pmu_wakeup_source_get(g_modem.pmu, g_modem.wake_src,
                                  sizeof(g_modem.wake_src)) != 0) {
        g_modem.wake_src[0] = '\0';
        return;
    }
    MODEM_LOG("wakeup source: %s", g_modem.wake_src);
}

int modem_init(const modem_cfg_t *cfg)
{
    if (g_modem.inited)
        return 0;

    memset(&g_modem, 0, sizeof(g_modem));
    apply_defaults(cfg);

    if (g_modem.cfg.req_ready_gpio < 0 ||
        g_modem.cfg.req_ready_gpio >= FPIOA_PIN_MAX_NUM) {
        MODEM_LOG("invalid req_ready gpio %d", g_modem.cfg.req_ready_gpio);
        return -1;
    }

    drv_fpioa_set_pin_func(g_modem.cfg.req_ready_gpio,
                           GPIO0 + g_modem.cfg.req_ready_gpio);

    if (drv_gpio_inst_create(g_modem.cfg.req_ready_gpio,
                             &g_modem.req_ready) != 0) {
        MODEM_LOG("create req_ready gpio %d failed",
                  g_modem.cfg.req_ready_gpio);
        return -1;
    }

    if (drv_gpio_mode_set(g_modem.req_ready, GPIO_DM_OUTPUT) != 0) {
        MODEM_LOG("set req_ready output failed");
        drv_gpio_inst_destroy(&g_modem.req_ready);
        return -1;
    }
    req_ready_set(0);

    if (drv_pmu_inst_create(&g_modem.pmu) != 0) {
        MODEM_LOG("PMU unavailable; shutdown/wakeup-source disabled");
        g_modem.pmu = NULL;
    }

    latch_wakeup_source();

    g_modem.inited = 1;
    MODEM_LOG("init ok: req_ready=GPIO%d at=%s",
              g_modem.cfg.req_ready_gpio, g_modem.cfg.at_dev);
    return 0;
}

void modem_deinit(void)
{
    if (!g_modem.inited)
        return;

    if (g_modem.req_ready)
        drv_gpio_inst_destroy(&g_modem.req_ready);
    if (g_modem.pmu)
        drv_pmu_inst_destroy(&g_modem.pmu);

    g_modem.inited = 0;
}

const char *board_get_wakeup_source(void)
{
    return (g_modem.inited && g_modem.wake_src[0]) ? g_modem.wake_src : NULL;
}

int modem_wake(void)
{
    if (!g_modem.inited) {
        MODEM_LOG("wake before init");
        return -1;
    }

    if (wake_attempt() == 0) {
        return 0;
    }

    MODEM_LOG("wake failed");
    return -1;
}

int modem_get_imei(char *buf, size_t size)
{
    char resp[256];
    int  fd, rc, i, start = -1;

    if (!g_modem.inited || !buf || size < MODEM_IMEI_LEN + 1)
        return -1;

    if (g_modem.imei_valid) {
        memcpy(buf, g_modem.imei, MODEM_IMEI_LEN + 1);
        return 0;
    }

    fd = at_open();
    if (fd < 0)
        return -1;

    rc = at_cmd(fd, "AT+CGSN=1", resp, sizeof(resp), 5000);
    close(fd);
    if (rc != 1) {
        MODEM_LOG("AT+CGSN=1 failed (%d)", rc);
        return -1;
    }

    for (i = 0; resp[i]; ) {
        int j;

        if (resp[i] < '0' || resp[i] > '9') {
            i++;
            continue;
        }
        for (j = i; resp[j] >= '0' && resp[j] <= '9'; j++)
            ;
        if (j - i == MODEM_IMEI_LEN) {
            start = i;
            break;
        }
        i = j;
    }

    if (start < 0) {
        MODEM_LOG("no IMEI in response");
        return -1;
    }

    memcpy(g_modem.imei, resp + start, MODEM_IMEI_LEN);
    g_modem.imei[MODEM_IMEI_LEN] = '\0';
    g_modem.imei_valid = 1;

    memcpy(buf, g_modem.imei, MODEM_IMEI_LEN + 1);
    MODEM_LOG("IMEI %s", g_modem.imei);
    return 0;
}

int modem_enter_edrx(void)
{
    if (!g_modem.inited) {
        MODEM_LOG("enter_edrx before init");
        return -1;
    }

    req_ready_set(0);
    MODEM_LOG("REQ_READY low, shutting down");

    if (!g_modem.pmu) {
        MODEM_LOG("no PMU handle; cannot shut down");
        return -1;
    }

    drv_pmu_shutdown_now(g_modem.pmu);

    MODEM_LOG("shutdown did not take effect");
    return -1;
}
