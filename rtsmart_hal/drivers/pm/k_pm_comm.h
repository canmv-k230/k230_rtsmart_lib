/**
 * @file k_pm_comm.h
 * @brief PM userspace compatibility types for /dev/pm
 */
#ifndef __K_PM_COMM_H__
#define __K_PM_COMM_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_DOMAIN_CPU,
    PM_DOMAIN_KPU,
    PM_DOMAIN_DPU,
    PM_DOMAIN_VPU,
    PM_DOMAIN_DISPLAY,
    PM_DOMAIN_MEDIA,
    PM_DOMAIN_NR,
} k_pm_domain;

typedef enum {
    PM_GOVERNOR_MANUAL,
    PM_GOVERNOR_PERFORMANCE,
    PM_GOVERNOR_ENERGYSAVING,
    PM_GOVERNOR_AUTO,
} k_pm_governor;

typedef struct {
    int32_t freq;
    int32_t volt;
} k_pm_profile;

/* Must match K230_PM_MAX_PROFILE_COUNT in the kernel /dev/pm UAPI. */
#define K_PM_MAX_PROFILE_COUNT 16U

#define K_PM_ERR_ID_ILLEGAL_PARAM 3
#define K_PM_ERR_ID_EXIST         4
#define K_PM_ERR_ID_UNEXIST       5
#define K_PM_ERR_ID_NULL_PTR      6
#define K_PM_ERR_ID_NOT_CONFIG    7
#define K_PM_ERR_ID_NOT_SUPPORT   8
#define K_PM_ERR_ID_NOT_PERM      9
#define K_PM_ERR_ID_NOMEM         12
#define K_PM_ERR_ID_NOTREADY      16
#define K_PM_ERR_ID_BADADDR       17
#define K_PM_ERR_ID_BUSY          18

/* Keep these ABI values synchronized with K_ERR_APPID, K_ID_PM and
 * K_ERR_LEVEL_ERROR in the MPP k_errno.h/k_module.h headers.  This PM HAL is
 * built independently, so it cannot include those headers directly.
 */
#define K_PM_ERR_APP_ID      0xA0000000UL
#define K_PM_MODULE_ID       23UL
#define K_PM_ERR_LEVEL_ERROR 4UL
#define K_ERR_PM(_errid) \
    ((int)(K_PM_ERR_APP_ID | (K_PM_MODULE_ID << 16) | \
        (K_PM_ERR_LEVEL_ERROR << 13) | (_errid)))

#define K_ERR_PM_ILLEGAL_PARAM K_ERR_PM(K_PM_ERR_ID_ILLEGAL_PARAM)
#define K_ERR_PM_EXIST         K_ERR_PM(K_PM_ERR_ID_EXIST)
#define K_ERR_PM_UNEXIST       K_ERR_PM(K_PM_ERR_ID_UNEXIST)
#define K_ERR_PM_NULL_PTR      K_ERR_PM(K_PM_ERR_ID_NULL_PTR)
#define K_ERR_PM_NOT_CONFIG    K_ERR_PM(K_PM_ERR_ID_NOT_CONFIG)
#define K_ERR_PM_NOT_SUPPORT   K_ERR_PM(K_PM_ERR_ID_NOT_SUPPORT)
#define K_ERR_PM_NOT_PERM      K_ERR_PM(K_PM_ERR_ID_NOT_PERM)
#define K_ERR_PM_NOMEM         K_ERR_PM(K_PM_ERR_ID_NOMEM)
#define K_ERR_PM_NOTREADY      K_ERR_PM(K_PM_ERR_ID_NOTREADY)
#define K_ERR_PM_BADADDR       K_ERR_PM(K_PM_ERR_ID_BADADDR)
#define K_ERR_PM_BUSY          K_ERR_PM(K_PM_ERR_ID_BUSY)

#ifdef __cplusplus
}
#endif

#endif
