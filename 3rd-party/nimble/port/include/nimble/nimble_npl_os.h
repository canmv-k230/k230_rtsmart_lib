/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RTSMART_NIMBLE_NPL_OS_H
#define RTSMART_NIMBLE_NPL_OS_H

#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>

#define BLE_NPL_OS_ALIGNMENT 8
#define BLE_NPL_TIME_FOREVER UINT32_MAX

typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;

struct ble_npl_event {
    ble_npl_event_fn *fn;
    void *arg;
    struct ble_npl_event *next;
    bool queued;
};

struct ble_npl_eventq {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct ble_npl_event *head;
    struct ble_npl_event *tail;
};

struct ble_npl_callout {
    struct ble_npl_event event;
    struct ble_npl_eventq *eventq;
    struct ble_npl_callout *next;
    ble_npl_time_t deadline;
    bool active;
    bool listed;
};

struct ble_npl_mutex {
    pthread_mutex_t lock;
};

struct ble_npl_sem {
    sem_t sem;
};

#endif /* RTSMART_NIMBLE_NPL_OS_H */
