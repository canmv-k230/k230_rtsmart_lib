/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
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
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "generated/autoconf.h"
#include "drv_fpioa.h"
#include "drv_gpio.h"
#include "drv_timer.h"

#define DRV_GPIO_DEV ("/dev/gpio")

/* ioctl */
#define KD_GPIO_IOCTL_SET_MODE _IOW('G', 0, gpio_cfg_t*)
#define KD_GPIO_IOCTL_GET_MODE _IOR('G', 1, gpio_cfg_t*)

#define KD_GPIO_IOCTL_SET_IRQ _IOW('G', 2, gpio_irqcfg_t*)
#define KD_GPIO_IOCTL_GET_IRQ _IOR('G', 3, gpio_irqcfg_t*)

#define KD_GPIO_IOCTL_CTRL_IRQ _IOWR('G', 4, gpio_cfg_t*)

#define KD_GPIO_SIG (SIGRTMIN + 1 + KD_TIMER_MAX_NUM)

typedef struct {
    uint16_t pin;
    uint16_t value;
} gpio_cfg_t;

typedef struct {
    uint16_t pin;
    uint16_t mode; // @ref gpio_pin_edge_t

    uint16_t debounce_ms;
    uint16_t signo;
    void*    sigval; // reuse as callback
} gpio_irqcfg_t;

static int gpio_fd      = -1;
static int gpio_ref_cnt = 0;
static pthread_mutex_t gpio_lock = PTHREAD_MUTEX_INITIALIZER;

static const int gpio_inst_type = 0;

static inline int drv_gpio_pin_enabled(int pin)
{
    if ((pin < 0) || (pin >= GPIO_MAX_NUM)) {
        return 0;
    }

#ifdef CONFIG_BOARD_NOT_SUPPORT_HW_RTC
    if (pin >= 64) {
        return 0;
    }
#endif

    return 1;
}

static int drv_gpio_open(void)
{
    int ret = 0;

    pthread_mutex_lock(&gpio_lock);
    if (0x00 > gpio_fd) {
        gpio_fd = open(DRV_GPIO_DEV, O_RDWR);
        if (0x00 > gpio_fd) {
            ret = -1;
            goto _unlock;
        }
    }

    gpio_ref_cnt++;

_unlock:
    pthread_mutex_unlock(&gpio_lock);

    if (ret) {
        printf("[hal_gpio]: open gpio device failed.\n");
    }

    return ret;
}

static void drv_gpio_close(void)
{
    pthread_mutex_lock(&gpio_lock);
    if (0x00 <= gpio_fd) {
        if ((gpio_ref_cnt > 0) && (0x00 == (--gpio_ref_cnt))) {
            close(gpio_fd);
            gpio_fd = -1;
        }
    }
    pthread_mutex_unlock(&gpio_lock);
}

static inline int drv_gpio_ioctl(int cmd, void* arg)
{
    int ret = 0;

    pthread_mutex_lock(&gpio_lock);
    if (0x00 > gpio_fd) {
        ret = -1;
        goto _unlock;
    }

    if (0x00 != ioctl(gpio_fd, cmd, arg)) {
        ret = -1;
    }

_unlock:
    pthread_mutex_unlock(&gpio_lock);

    if (ret && (0x00 > gpio_fd)) {
        printf("[hal_gpio]: gpio not open\n");
    }

    return ret;
}

int drv_gpio_inst_create(int pin, drv_gpio_inst_t** inst)
{
    fpioa_func_t pin_curr_func;

    if (NULL == inst) {
        return -1;
    }

    if (!drv_gpio_pin_enabled(pin)) {
        printf("[hal_gpio]: invalid pin %d\n", pin);
        return -1;
    }

    if ((0x00 != drv_fpioa_get_pin_func(pin, &pin_curr_func)) || (pin_curr_func != (fpioa_func_t)(GPIO0 + pin))) {
        printf("[hal_gpio]: pin %d current fucntion not GPIO\n", pin);
        return -1;
    }

    if (0x00 != drv_gpio_open()) {
        return -1;
    }

    if (*inst) {
        drv_gpio_inst_destroy(inst);
        *inst = NULL;
    }

    *inst = malloc(sizeof(drv_gpio_inst_t));
    if (NULL == *inst) {
        printf("[hal_gpio]: malloc failed");
        drv_gpio_close();
        return -1;
    }

    (*inst)->base          = (void*)&gpio_inst_type;
    (*inst)->pin           = pin;
    (*inst)->curr_val      = -1;
    (*inst)->curr_mode     = GPIO_DM_MAX;
    (*inst)->curr_irq_mode = GPIO_PE_MAX;
    (*inst)->irq_args      = NULL;
    (*inst)->irq_callback  = NULL;

    return 0;
}

void drv_gpio_inst_destroy(drv_gpio_inst_t** inst)
{
    if (NULL == inst) {
        return;
    }

    if (!*inst) {
        return;
    }

    if ((void*)&gpio_inst_type != (*inst)->base) {
        printf("[hal_gpio]: inst not gpio\n");
        return;
    }

    drv_gpio_mode_set(*inst, GPIO_DM_INPUT);
    drv_gpio_close();

    free(*inst);
    *inst = NULL;
}

int drv_gpio_value_set(drv_gpio_inst_t* inst, gpio_pin_value_t val)
{
    uint8_t value = val;
    int     ret   = -1;
    int     gpio_not_open = 0;

    if (NULL == inst) {
        return -1;
    }

    pthread_mutex_lock(&gpio_lock);
    if (0x00 > gpio_fd) {
        gpio_not_open = 1;
        goto _unlock;
    }

    if (inst->curr_val == val) {
        ret = 0;
        goto _unlock;
    }

    if ((off_t)inst->pin != lseek(gpio_fd, inst->pin, SEEK_SET)) {
        goto _unlock;
    }

    if (0x01 == write(gpio_fd, &value, 1)) {
        inst->curr_val = val;
        ret            = 0;
    }

_unlock:
    pthread_mutex_unlock(&gpio_lock);

    if (ret) {
        if (gpio_not_open) {
            printf("[hal_gpio]: gpio not open\n");
        } else {
            printf("[hal_gpio]: set pin%d failed\n", inst->pin);
        }
    }

    return ret;
}

gpio_pin_value_t drv_gpio_value_get(drv_gpio_inst_t* inst)
{
    uint8_t value = 0;

    if (NULL == inst) {
        return GPIO_PV_LOW;
    }

    pthread_mutex_lock(&gpio_lock);
    if (0x00 > gpio_fd) {
        pthread_mutex_unlock(&gpio_lock);
        printf("[hal_gpio]: gpio not open\n");
        return -1;
    }

    if ((off_t)inst->pin != lseek(gpio_fd, inst->pin, SEEK_SET)) {
        pthread_mutex_unlock(&gpio_lock);
        printf("[hal_gpio]: get pin%d failed\n", inst->pin);
        return -1;
    }

    if (0x01 != read(gpio_fd, &value, 1)) {
        pthread_mutex_unlock(&gpio_lock);
        printf("[hal_gpio]: get pin%d failed\n", inst->pin);
        return -1;
    }

    inst->curr_val = value;

    pthread_mutex_unlock(&gpio_lock);

    return value;
}

int drv_gpio_mode_set(drv_gpio_inst_t* inst, gpio_drive_mode_t mode)
{
    if (NULL == inst) {
        return -1;
    }

    gpio_cfg_t cfg = { .pin = inst->pin, mode };

    if (mode == inst->curr_mode) {
        return 0;
    }

    if (0x00 != drv_gpio_ioctl(KD_GPIO_IOCTL_SET_MODE, &cfg)) {
        return -1;
    }

    inst->curr_mode = mode;

    return 0;
}

gpio_drive_mode_t drv_gpio_mode_get(drv_gpio_inst_t* inst)
{
    if (NULL == inst) {
        return GPIO_DM_MAX;
    }

    int        ret;
    gpio_cfg_t cfg = { .pin = inst->pin };

    if (0x00 != (ret = drv_gpio_ioctl(KD_GPIO_IOCTL_GET_MODE, &cfg))) {
        printf("[hal_gpio]: read pin %d mode failed %d\n", cfg.pin, ret);
        return GPIO_DM_MAX;
    }
    inst->curr_mode = cfg.value;

    return cfg.value;
}

int drv_gpio_set_irq(drv_gpio_inst_t* inst, int enable)
{
    if (NULL == inst) {
        return -1;
    }
    gpio_cfg_t cfg = { .pin = inst->pin, enable };

    cfg.value &= ~(1 << 7); // just enable or disable irq, not auto detach irq

    return drv_gpio_ioctl(KD_GPIO_IOCTL_CTRL_IRQ, &cfg);
}

static void drv_gpio_sig_handler(int sig, siginfo_t* si, void* uc)
{
    (void)uc;

    drv_gpio_inst_t* inst = si->si_ptr;

    if (SI_SIGIO != si->si_code) {
        return;
    }

    if (!inst || (&gpio_inst_type != inst->base)) {
        return;
    }

    if (inst->signo != sig) {
        return;
    }

    if (inst->irq_callback) {
        inst->irq_callback(inst->irq_args);
    }
}

int drv_gpio_register_irq(drv_gpio_inst_t* inst, gpio_pin_edge_t mode, int debounce, gpio_irq_callback callback, void* userargs)
{
    int              ret;
    struct sigaction sa;
    static int register_cnt;

    if (NULL == inst) {
        return -1;
    }

    if (GPIO_PE_MAX <= mode) {
        printf("[hal_gpio]: edge mode only support %d~%d, not %d\n", GPIO_PE_RISING, GPIO_PE_LOW, mode);
        return -1;
    }

    gpio_irqcfg_t cfg = { .pin = inst->pin, .mode = mode, .debounce_ms = 10 };

    if (GPIO_IRQ_MAX_NUM <= inst->pin) {
        printf("[hal_gpio]: pin irq only support 0~63, not %d\n", inst->pin);
        return -1;
    }

    if (10 > debounce) {
        debounce = 10;
    }
    cfg.debounce_ms = debounce;

    if (GPIO_PE_MAX != inst->curr_irq_mode) {
        if (0x00 != drv_gpio_unregister_irq(inst)) {
            return -1;
        }
    }

    inst->signo = (KD_GPIO_SIG + (register_cnt++ % 8));

    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = drv_gpio_sig_handler;
    sigemptyset(&sa.sa_mask);
    if ((-1) == sigaction(inst->signo, &sa, NULL)) {
        printf("[hal_gpio]: register sigaction failed.\n");
        return -1;
    }

    cfg.signo  = inst->signo;
    cfg.sigval = inst;
    if (0x00 != (ret = drv_gpio_ioctl(KD_GPIO_IOCTL_SET_IRQ, &cfg))) {
        printf("[hal_gpio]: set pin %d irq failed %d\n", cfg.pin, ret);

        sa.sa_handler   = SIG_IGN;
        sa.sa_sigaction = NULL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(inst->signo, &sa, NULL);

        return -1;
    }

    inst->curr_irq_mode = mode;
    inst->irq_args      = userargs;
    inst->irq_callback  = callback;

    return 0;
}

int drv_gpio_unregister_irq(drv_gpio_inst_t* inst)
{
    if (NULL == inst) {
        return -1;
    }

    int              ret;
    struct sigaction sa;
    gpio_cfg_t       cfg = { .pin = inst->pin, 0 };

    cfg.value |= (1 << 7); // disable irq and detach irq.

    if ((GPIO_PE_MAX == inst->curr_irq_mode) && (NULL == inst->irq_callback)) {
        return 0;
    }

    if (0x00 != (ret = drv_gpio_ioctl(KD_GPIO_IOCTL_CTRL_IRQ, &cfg))) {
        printf("[hal_gpio]: disable pin %d irq failed %d\n", cfg.pin, ret);
        return -1;
    }

    inst->curr_irq_mode = GPIO_PE_MAX;
    inst->irq_args      = NULL;
    inst->irq_callback  = NULL;

    sa.sa_handler   = SIG_IGN;
    sa.sa_sigaction = NULL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(inst->signo, &sa, NULL);

    return 0;
}
