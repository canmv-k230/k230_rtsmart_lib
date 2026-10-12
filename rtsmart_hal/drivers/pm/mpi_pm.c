/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
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
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>
#include "k_pm_ioctl.h"
#include "mpi_pm_api.h"

static int g_pm_fd = -1;
static pthread_mutex_t g_pm_mutex = PTHREAD_MUTEX_INITIALIZER;

static int pm_check_open(void)
{
    int saved_errno;

    pthread_mutex_lock(&g_pm_mutex);
    if (g_pm_fd < 0) {
        g_pm_fd = open("/dev/pm", O_RDWR);
        if (g_pm_fd < 0) {
            saved_errno = errno;
            perror("open err\n");
            pthread_mutex_unlock(&g_pm_mutex);
            return -saved_errno;
        }
    }
    pthread_mutex_unlock(&g_pm_mutex);

    return 0;
}

static int pm_convert_ret(int ret)
{
    if (ret <= 0)
        return ret;

    switch (ret) {
    case K_PM_ERR_ID_ILLEGAL_PARAM:
    case K_PM_ERR_ID_EXIST:
    case K_PM_ERR_ID_UNEXIST:
    case K_PM_ERR_ID_NULL_PTR:
    case K_PM_ERR_ID_NOT_CONFIG:
    case K_PM_ERR_ID_NOT_SUPPORT:
    case K_PM_ERR_ID_NOT_PERM:
    case K_PM_ERR_ID_NOMEM:
    case K_PM_ERR_ID_NOTREADY:
    case K_PM_ERR_ID_BADADDR:
    case K_PM_ERR_ID_BUSY:
        return K_ERR_PM(ret);
    default:
        return ret;
    }
}

static int pm_ioctl(unsigned long request, void *arg)
{
    int ret;
    int saved_errno;

    ret = ioctl(g_pm_fd, request, arg);
    if (ret == -1) {
        saved_errno = errno;
        return -saved_errno;
    }

    return pm_convert_ret(ret);
}

int kd_mpi_pm_set_reg(uint32_t reg, uint32_t val)
{
    (void)reg;
    (void)val;

    return K_ERR_PM_NOT_SUPPORT;
}

int kd_mpi_pm_get_reg(uint32_t reg, uint32_t *pval)
{
    (void)reg;

    if (pval == NULL)
        return K_ERR_PM_NULL_PTR;

    return K_ERR_PM_NOT_SUPPORT;
}

int kd_mpi_pm_get_profiles(k_pm_domain domain, uint32_t *pcount,
    k_pm_profile *pprofile)
{
    int ret;
    _k_pm_domain_profiles *profiles;
    uint32_t request_count;
    uint32_t alloc_count;
    uint32_t returned_count;

    ret = pm_check_open();
    if (ret)
        return ret;

    if (pcount == NULL)
        return K_ERR_PM_NULL_PTR;

    request_count = *pcount;
    if (request_count != 0 && pprofile == NULL)
        return K_ERR_PM_NULL_PTR;
    if (request_count > K_PM_MAX_PROFILE_COUNT)
        request_count = K_PM_MAX_PROFILE_COUNT;

    alloc_count = request_count != 0 ? request_count : K_PM_MAX_PROFILE_COUNT;
    profiles = malloc(sizeof(_k_pm_domain_profiles) + alloc_count *
        sizeof(k_pm_profile));
    if (!profiles)
        return K_ERR_PM_NOMEM;

    profiles->domain = domain;
    profiles->number = request_count;

    ret = pm_ioctl(K_IOC_PM_GET_PROFILES, profiles);
    if (ret) {
        free(profiles);
        return ret;
    }

    returned_count = profiles->number;
    if (returned_count > alloc_count)
        returned_count = alloc_count;

    if (request_count != 0 && returned_count != 0)
        memcpy(pprofile, profiles->profiles, returned_count *
            sizeof(k_pm_profile));
    *pcount = returned_count;
    free(profiles);

    return 0;
}

int kd_mpi_pm_get_stat(k_pm_domain domain)
{
    int ret;

    ret = pm_check_open();
    if (ret)
        return ret;

    return pm_ioctl(K_IOC_PM_GET_STAT, &domain);
}

int kd_mpi_pm_set_governor(k_pm_domain domain, k_pm_governor governor)
{
    int ret;
    _k_pm_domain_governor gov;

    ret = pm_check_open();
    if (ret)
        return ret;

    gov.domain = domain;
    gov.governor = governor;

    ret = pm_ioctl(K_IOC_PM_SET_GOVERNOR, &gov);

    return ret;
}

int kd_mpi_pm_get_governor(k_pm_domain domain, k_pm_governor *pgovernor)
{
    int ret;
    _k_pm_domain_governor gov;

    if (pgovernor == NULL)
        return K_ERR_PM_NULL_PTR;

    ret = pm_check_open();
    if (ret)
        return ret;

    gov.domain = domain;

    ret = pm_ioctl(K_IOC_PM_GET_GOVERNOR, &gov);
    if (!ret)
        *pgovernor = gov.governor;

    return ret;
}

int kd_mpi_pm_set_profile(k_pm_domain domain, int32_t index)
{
    int ret;
    _k_pm_domain_profile profile;

    ret = pm_check_open();
    if (ret)
        return ret;

    profile.domain = domain;
    profile.index = index;

    ret = pm_ioctl(K_IOC_PM_SET_PROFILE, &profile);

    return ret;
}

int kd_mpi_pm_get_profile(k_pm_domain domain, int32_t *pindex)
{
    int ret;
    _k_pm_domain_profile profile;

    if (pindex == NULL)
        return K_ERR_PM_NULL_PTR;

    ret = pm_check_open();
    if (ret)
        return ret;

    profile.domain = domain;

    ret = pm_ioctl(K_IOC_PM_GET_PROFILE, &profile);
    if (!ret)
        *pindex = profile.index;

    return ret;
}

int kd_mpi_pm_set_profile_lock(k_pm_domain domain, int32_t index)
{
    int ret;
    _k_pm_domain_profile profile;

    ret = pm_check_open();
    if (ret)
        return ret;

    profile.domain = domain;
    profile.index = index;

    ret = pm_ioctl(K_IOC_PM_SET_PROFILE_LOCK, &profile);

    return ret;
}

int kd_mpi_pm_set_profile_unlock(k_pm_domain domain, int32_t index)
{
    int ret;
    _k_pm_domain_profile profile;

    ret = pm_check_open();
    if (ret)
        return ret;

    profile.domain = domain;
    profile.index = index;

    ret = pm_ioctl(K_IOC_PM_SET_PROFILE_UNLOCK, &profile);

    return ret;
}

int kd_mpi_pm_set_thermal_protect(k_pm_domain domain, int32_t temp,
    int32_t index)
{
    int ret;
    _k_pm_domain_thermal_protect thermal;

    ret = pm_check_open();
    if (ret)
        return ret;

    thermal.domain = domain;
    thermal.temp = temp;
    thermal.index = index;

    ret = pm_ioctl(K_IOC_PM_SET_THERMAL_PROTECT, &thermal);

    return ret;
}

int kd_mpi_pm_get_thermal_protect(k_pm_domain domain, int32_t *ptemp,
    int32_t *pindex)
{
    int ret;
    _k_pm_domain_thermal_protect thermal;

    if (ptemp == NULL || pindex == NULL)
        return K_ERR_PM_NULL_PTR;

    ret = pm_check_open();
    if (ret)
        return ret;

    thermal.domain = domain;

    ret = pm_ioctl(K_IOC_PM_GET_THERMAL_PROTECT, &thermal);
    if (!ret) {
        *ptemp = thermal.temp;
        *pindex = thermal.index;
    }

    return ret;
}

int kd_mpi_pm_set_thermal_shutdown(int32_t temp)
{
    int ret;

    ret = pm_check_open();
    if (ret)
        return ret;

    ret = pm_ioctl(K_IOC_PM_SET_THERMAL_SHUTDOWN, &temp);

    return ret;
}

int kd_mpi_pm_get_thermal_shutdown(int32_t *ptemp)
{
    int ret;
    int32_t temp;

    if (ptemp == NULL)
        return K_ERR_PM_NULL_PTR;

    ret = pm_check_open();
    if (ret)
        return ret;

    ret = pm_ioctl(K_IOC_PM_GET_THERMAL_SHUTDOWN, &temp);
    if (!ret)
        *ptemp = temp;

    return ret;
}

int kd_mpi_pm_set_clock(k_pm_domain domain, bool enable)
{
    int ret;
    _k_pm_domain_bool domain_bool;

    ret = pm_check_open();
    if (ret)
        return ret;

    domain_bool.domain = domain;
    domain_bool.enable = enable;

    ret = pm_ioctl(K_IOC_PM_SET_CLOCK, &domain_bool);

    return ret;
}

int kd_mpi_pm_set_power(k_pm_domain domain, bool enable)
{
    int ret;
    _k_pm_domain_bool domain_bool;

    ret = pm_check_open();
    if (ret)
        return ret;

    domain_bool.domain = domain;
    domain_bool.enable = enable;

    ret = pm_ioctl(K_IOC_PM_SET_POWER, &domain_bool);

    return ret;
}

__attribute__((weak)) int kd_mpi_pm_runtime_runstage(k_runtimestage_id stage)
{
    int ret = 0;

    if (stage == RUNTIMESTAGE_ID_AI2D_START ||
        stage == RUNTIMESTAGE_ID_KPU_START) {
        ret = kd_mpi_pm_set_clock(PM_DOMAIN_KPU, true);
        if (!ret) {
            ret = kd_mpi_pm_set_power(PM_DOMAIN_KPU, true);
            if (ret)
                (void)kd_mpi_pm_set_clock(PM_DOMAIN_KPU, false);
        }
    } else if (stage == RUNTIMESTAGE_ID_AI2D_STOP ||
               stage == RUNTIMESTAGE_ID_KPU_STOP) {
        ret = kd_mpi_pm_set_power(PM_DOMAIN_KPU, false);
        if (!ret) {
            ret = kd_mpi_pm_set_clock(PM_DOMAIN_KPU, false);
            if (ret)
                (void)kd_mpi_pm_set_power(PM_DOMAIN_KPU, true);
        }
    } else {
        ret = 0;
    }

    return ret;
}
