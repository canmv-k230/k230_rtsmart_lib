/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nimble/nimble_npl.h"

#include <errno.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t critical_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t critical_lock;
static _Thread_local unsigned int critical_depth;

static pthread_once_t callout_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t callout_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t callout_cond = PTHREAD_COND_INITIALIZER;
static struct ble_npl_callout *callout_list;
static bool callout_thread_ready;

static void absolute_realtime_after(uint32_t milliseconds, struct timespec *time)
{
    clock_gettime(CLOCK_REALTIME, time);
    time->tv_sec += milliseconds / 1000;
    time->tv_nsec += (long)(milliseconds % 1000) * 1000000L;
    if (time->tv_nsec >= 1000000000L) {
        time->tv_sec++;
        time->tv_nsec -= 1000000000L;
    }
}

static void critical_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&critical_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

bool ble_npl_os_started(void)
{
    return true;
}

void *ble_npl_get_current_task_id(void)
{
    return (void *)(uintptr_t)pthread_self();
}

void ble_npl_eventq_init(struct ble_npl_eventq *eventq)
{
    memset(eventq, 0, sizeof(*eventq));
    pthread_mutex_init(&eventq->lock, NULL);
    pthread_cond_init(&eventq->cond, NULL);
}

struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *eventq,
                                         ble_npl_time_t timeout)
{
    struct ble_npl_event *event = NULL;
    struct timespec until;
    int result = 0;

    pthread_mutex_lock(&eventq->lock);
    if (timeout != 0 && timeout != BLE_NPL_TIME_FOREVER) {
        absolute_realtime_after(timeout, &until);
    }
    while (!eventq->head && result == 0) {
        if (timeout == 0) {
            break;
        }
        if (timeout == BLE_NPL_TIME_FOREVER) {
            result = pthread_cond_wait(&eventq->cond, &eventq->lock);
        } else {
            result = pthread_cond_timedwait(&eventq->cond, &eventq->lock, &until);
        }
    }
    if (eventq->head) {
        event = eventq->head;
        eventq->head = event->next;
        if (!eventq->head) {
            eventq->tail = NULL;
        }
        event->next = NULL;
        event->queued = false;
    }
    pthread_mutex_unlock(&eventq->lock);
    return event;
}

void ble_npl_eventq_put(struct ble_npl_eventq *eventq, struct ble_npl_event *event)
{
    pthread_mutex_lock(&eventq->lock);
    if (!event->queued) {
        event->queued = true;
        event->next = NULL;
        if (eventq->tail) {
            eventq->tail->next = event;
        } else {
            eventq->head = event;
        }
        eventq->tail = event;
        pthread_cond_signal(&eventq->cond);
    }
    pthread_mutex_unlock(&eventq->lock);
}

void ble_npl_eventq_remove(struct ble_npl_eventq *eventq, struct ble_npl_event *event)
{
    struct ble_npl_event *current;
    struct ble_npl_event *previous = NULL;

    pthread_mutex_lock(&eventq->lock);
    for (current = eventq->head; current; current = current->next) {
        if (current == event) {
            if (previous) {
                previous->next = event->next;
            } else {
                eventq->head = event->next;
            }
            if (eventq->tail == event) {
                eventq->tail = previous;
            }
            event->next = NULL;
            event->queued = false;
            break;
        }
        previous = current;
    }
    pthread_mutex_unlock(&eventq->lock);
}

void ble_npl_event_init(struct ble_npl_event *event, ble_npl_event_fn *fn, void *arg)
{
    event->fn = fn;
    event->arg = arg;
    event->next = NULL;
    event->queued = false;
}

bool ble_npl_event_is_queued(struct ble_npl_event *event)
{
    return event->queued;
}

void *ble_npl_event_get_arg(struct ble_npl_event *event)
{
    return event->arg;
}

void ble_npl_event_set_arg(struct ble_npl_event *event, void *arg)
{
    event->arg = arg;
}

bool ble_npl_eventq_is_empty(struct ble_npl_eventq *eventq)
{
    bool empty;

    pthread_mutex_lock(&eventq->lock);
    empty = eventq->head == NULL;
    pthread_mutex_unlock(&eventq->lock);
    return empty;
}

void ble_npl_event_run(struct ble_npl_event *event)
{
    if (event && event->fn) {
        event->fn(event);
    }
}

ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mutex)
{
    pthread_mutexattr_t attr;
    int result;

    if (!mutex) {
        return BLE_NPL_EINVAL;
    }
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    result = pthread_mutex_init(&mutex->lock, &attr);
    pthread_mutexattr_destroy(&attr);
    return result == 0 ? BLE_NPL_OK : BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mutex,
                                   ble_npl_time_t timeout)
{
    struct timespec until;
    int result;

    if (!mutex) {
        return BLE_NPL_EINVAL;
    }
    if (timeout == BLE_NPL_TIME_FOREVER) {
        result = pthread_mutex_lock(&mutex->lock);
    } else if (timeout == 0) {
        result = pthread_mutex_trylock(&mutex->lock);
    } else {
        absolute_realtime_after(timeout, &until);
        result = pthread_mutex_timedlock(&mutex->lock, &until);
    }
    if (result == ETIMEDOUT || result == EBUSY) {
        return BLE_NPL_TIMEOUT;
    }
    return result == 0 ? BLE_NPL_OK : BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mutex)
{
    return mutex && pthread_mutex_unlock(&mutex->lock) == 0 ?
        BLE_NPL_OK : BLE_NPL_BAD_MUTEX;
}

ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    return sem && sem_init(&sem->sem, 0, tokens) == 0 ? BLE_NPL_OK : BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout)
{
    struct timespec until;
    int result;

    if (!sem) {
        return BLE_NPL_EINVAL;
    }
    do {
        if (timeout == BLE_NPL_TIME_FOREVER) {
            result = sem_wait(&sem->sem);
        } else if (timeout == 0) {
            result = sem_trywait(&sem->sem);
        } else {
            absolute_realtime_after(timeout, &until);
            result = sem_timedwait(&sem->sem, &until);
        }
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        return BLE_NPL_OK;
    }
    return errno == ETIMEDOUT || errno == EAGAIN ? BLE_NPL_TIMEOUT : BLE_NPL_ERROR;
}

ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem)
{
    return sem && sem_post(&sem->sem) == 0 ? BLE_NPL_OK : BLE_NPL_ERROR;
}

uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    int value = 0;

    if (sem) {
        sem_getvalue(&sem->sem, &value);
    }
    return value > 0 ? (uint16_t)value : 0;
}

static void *callout_worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&callout_lock);
    for (;;) {
        struct ble_npl_callout *callout;
        struct ble_npl_callout *due = NULL;
        ble_npl_time_t now = ble_npl_time_get();
        uint32_t wait_ms = UINT32_MAX;

        for (callout = callout_list; callout; callout = callout->next) {
            int32_t remaining;

            if (!callout->active) {
                continue;
            }
            remaining = (int32_t)(callout->deadline - now);
            if (remaining <= 0) {
                due = callout;
                break;
            }
            if ((uint32_t)remaining < wait_ms) {
                wait_ms = (uint32_t)remaining;
            }
        }

        if (due) {
            due->active = false;
            pthread_mutex_unlock(&callout_lock);
            if (due->eventq) {
                ble_npl_eventq_put(due->eventq, &due->event);
            } else {
                ble_npl_event_run(&due->event);
            }
            pthread_mutex_lock(&callout_lock);
        } else if (wait_ms == UINT32_MAX) {
            pthread_cond_wait(&callout_cond, &callout_lock);
        } else {
            struct timespec until;

            absolute_realtime_after(wait_ms, &until);
            pthread_cond_timedwait(&callout_cond, &callout_lock, &until);
        }
    }
    return NULL;
}

static void callout_manager_init(void)
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, callout_worker, NULL) == 0) {
        pthread_detach(thread);
        callout_thread_ready = true;
    }
}

void ble_npl_callout_init(struct ble_npl_callout *callout,
                          struct ble_npl_eventq *eventq,
                          ble_npl_event_fn *fn, void *arg)
{
    memset(callout, 0, sizeof(*callout));
    ble_npl_event_init(&callout->event, fn, arg);
    callout->eventq = eventq;
    pthread_once(&callout_once, callout_manager_init);
}

ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *callout,
                                      ble_npl_time_t ticks)
{
    if (!callout || !callout_thread_ready) {
        return BLE_NPL_ERROR;
    }
    if (ticks == 0) {
        ticks = 1;
    }

    pthread_mutex_lock(&callout_lock);
    if (!callout->listed) {
        callout->next = callout_list;
        callout_list = callout;
        callout->listed = true;
    }
    callout->deadline = ble_npl_time_get() + ticks;
    callout->active = true;
    pthread_cond_signal(&callout_cond);
    pthread_mutex_unlock(&callout_lock);
    return BLE_NPL_OK;
}

void ble_npl_callout_stop(struct ble_npl_callout *callout)
{
    if (!callout) {
        return;
    }
    pthread_mutex_lock(&callout_lock);
    callout->active = false;
    pthread_cond_signal(&callout_cond);
    pthread_mutex_unlock(&callout_lock);
}

bool ble_npl_callout_is_active(struct ble_npl_callout *callout)
{
    bool active;

    pthread_mutex_lock(&callout_lock);
    active = callout && callout->active;
    pthread_mutex_unlock(&callout_lock);
    return active;
}

ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *callout)
{
    return callout ? callout->deadline : 0;
}

ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *callout,
                                               ble_npl_time_t now)
{
    int32_t remaining;

    if (!callout) {
        return 0;
    }
    pthread_mutex_lock(&callout_lock);
    remaining = callout->active ? (int32_t)(callout->deadline - now) : 0;
    pthread_mutex_unlock(&callout_lock);
    return remaining > 0 ? (ble_npl_time_t)remaining : 0;
}

void ble_npl_callout_set_arg(struct ble_npl_callout *callout, void *arg)
{
    if (callout) {
        callout->event.arg = arg;
    }
}

ble_npl_time_t ble_npl_time_get(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (ble_npl_time_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *ticks)
{
    if (!ticks) {
        return BLE_NPL_EINVAL;
    }
    *ticks = ms;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *ms)
{
    if (!ms) {
        return BLE_NPL_EINVAL;
    }
    *ms = ticks;
    return BLE_NPL_OK;
}

ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms)
{
    return ms;
}

uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks)
{
    return ticks;
}

void ble_npl_time_delay(ble_npl_time_t ticks)
{
    struct timespec delay = {
        .tv_sec = ticks / 1000,
        .tv_nsec = (long)(ticks % 1000) * 1000000L,
    };

    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

uint32_t ble_npl_hw_enter_critical(void)
{
    pthread_once(&critical_once, critical_init);
    pthread_mutex_lock(&critical_lock);
    critical_depth++;
    return critical_depth;
}

void ble_npl_hw_exit_critical(uint32_t ctx)
{
    (void)ctx;
    if (critical_depth) {
        critical_depth--;
        pthread_mutex_unlock(&critical_lock);
    }
}

bool ble_npl_hw_is_in_critical(void)
{
    return critical_depth != 0;
}
