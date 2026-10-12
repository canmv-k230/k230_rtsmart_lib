/**
 * @file k_pm_ioctl.h
 * @brief PM userspace compatibility ioctl ABI for /dev/pm
 */
#ifndef __K_PM_IOCTL_H__
#define __K_PM_IOCTL_H__

#include "k_pm_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _IOC
#define _IOC(a, b, c, d) (((a) << 30) | ((b) << 8) | (c) | ((d) << 16))
#define _IOC_NONE  0U
#define _IOC_WRITE 1U
#define _IOC_READ  2U
#define _IO(a, b)       _IOC(_IOC_NONE, (a), (b), 0)
#define _IOW(a, b, c)   _IOC(_IOC_WRITE, (a), (b), sizeof(c))
#define _IOR(a, b, c)   _IOC(_IOC_READ, (a), (b), sizeof(c))
#define _IOWR(a, b, c)  _IOC(_IOC_READ | _IOC_WRITE, (a), (b), sizeof(c))
#endif

#define K_IOC_TYPE_PM 'p'

typedef struct {
    k_pm_domain domain;
    uint32_t number;
    k_pm_profile profiles[0];
} _k_pm_domain_profiles;

typedef struct {
    k_pm_domain domain;
    k_pm_governor governor;
} _k_pm_domain_governor;

typedef struct {
    k_pm_domain domain;
    int32_t index;
} _k_pm_domain_profile;

typedef struct {
    k_pm_domain domain;
    int32_t temp; /* unit: C */
    int32_t index;
} _k_pm_domain_thermal_protect;

typedef struct {
    k_pm_domain domain;
    bool enable;
} _k_pm_domain_bool;

enum {
    K_IOC_NR_PM_RESERVED0,
    K_IOC_NR_PM_RESERVED1,
    K_IOC_NR_PM_GET_PROFILES,
    K_IOC_NR_PM_GET_STAT,
    K_IOC_NR_PM_SET_GOVERNOR,
    K_IOC_NR_PM_GET_GOVERNOR,
    K_IOC_NR_PM_SET_PROFILE,
    K_IOC_NR_PM_GET_PROFILE,
    K_IOC_NR_PM_SET_PROFILE_LOCK,
    K_IOC_NR_PM_SET_PROFILE_UNLOCK,
    K_IOC_NR_PM_SET_THERMAL_PROTECT,
    K_IOC_NR_PM_GET_THERMAL_PROTECT,
    K_IOC_NR_PM_SET_THERMAL_SHUTDOWN,
    K_IOC_NR_PM_GET_THERMAL_SHUTDOWN,
    K_IOC_NR_PM_SET_CLOCK,
    K_IOC_NR_PM_SET_POWER,
};

#define K_IOC_PM_GET_PROFILES \
    _IOWR(K_IOC_TYPE_PM, K_IOC_NR_PM_GET_PROFILES, _k_pm_domain_profiles)
#define K_IOC_PM_GET_STAT \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_GET_STAT, k_pm_domain)
#define K_IOC_PM_SET_GOVERNOR \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_GOVERNOR, _k_pm_domain_governor)
#define K_IOC_PM_GET_GOVERNOR \
    _IOWR(K_IOC_TYPE_PM, K_IOC_NR_PM_GET_GOVERNOR, _k_pm_domain_governor)
#define K_IOC_PM_SET_PROFILE \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_PROFILE, _k_pm_domain_profile)
#define K_IOC_PM_GET_PROFILE \
    _IOWR(K_IOC_TYPE_PM, K_IOC_NR_PM_GET_PROFILE, _k_pm_domain_profile)
#define K_IOC_PM_SET_PROFILE_LOCK \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_PROFILE_LOCK, _k_pm_domain_profile)
#define K_IOC_PM_SET_PROFILE_UNLOCK \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_PROFILE_UNLOCK, _k_pm_domain_profile)
#define K_IOC_PM_SET_THERMAL_PROTECT \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_THERMAL_PROTECT, _k_pm_domain_thermal_protect)
#define K_IOC_PM_GET_THERMAL_PROTECT \
    _IOWR(K_IOC_TYPE_PM, K_IOC_NR_PM_GET_THERMAL_PROTECT, _k_pm_domain_thermal_protect)
#define K_IOC_PM_SET_THERMAL_SHUTDOWN \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_THERMAL_SHUTDOWN, int32_t)
#define K_IOC_PM_GET_THERMAL_SHUTDOWN \
    _IOWR(K_IOC_TYPE_PM, K_IOC_NR_PM_GET_THERMAL_SHUTDOWN, int32_t)
#define K_IOC_PM_SET_CLOCK \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_CLOCK, _k_pm_domain_bool)
#define K_IOC_PM_SET_POWER \
    _IOW(K_IOC_TYPE_PM, K_IOC_NR_PM_SET_POWER, _k_pm_domain_bool)

#ifdef __cplusplus
}
#endif

#endif
