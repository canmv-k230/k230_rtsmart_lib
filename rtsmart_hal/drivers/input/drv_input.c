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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "drv_input.h"

static const int _drv_input_inst_type;

#define DRV_INPUT_POLL_SLICE_MS    (200)

static bool drv_input_ret_is_disconnect(int ret)
{
    return ret == -ENODEV || ret == -EIO || ret == -ENXIO;
}

static void drv_input_cache_info(drv_input_inst_t *inst, const struct drv_input_info *info)
{
    if (inst == NULL || info == NULL) {
        return;
    }

    inst->info = *info;
}

static bool drv_input_path_rebound(drv_input_inst_t *inst)
{
    struct drv_input_info info;
    int ret;

    if (inst == NULL || inst->fd < 0) {
        return true;
    }

    if (inst->info.kind == DRV_INPUT_DEV_UNKNOWN) {
        return false;
    }

    memset(&info, 0, sizeof(info));
    ret = ioctl(inst->fd, DRV_INPUT_CTRL_GET_INFO, &info);
    if (ret < 0) {
        return true;
    }

    return info.kind != inst->info.kind ||
           info.ev_bits != inst->info.ev_bits ||
           info.key_bits != inst->info.key_bits ||
           info.rel_bits != inst->info.rel_bits ||
           info.abs_bits != inst->info.abs_bits ||
           strncmp(info.name, inst->info.name, sizeof(info.name)) != 0;
}

static int drv_input_poll_once(drv_input_inst_t *inst, int timeout_ms)
{
    struct pollfd fds;
    int ret;

    fds.fd = inst->fd;
    fds.events = POLLIN;
    fds.revents = 0;

    ret = poll(&fds, 1, timeout_ms);
    if (ret < 0) {
        return -errno;
    }

    if (ret > 0 && (fds.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return -EIO;
    }

    return ret;
}

static int drv_input_open_path(const char *path, drv_input_inst_t **inst, int id)
{
    int fd;

    if (path == NULL || inst == NULL) {
        return -EINVAL;
    }

    if (*inst) {
        drv_input_inst_destroy(inst);
        *inst = NULL;
    }

    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return -errno;
    }

    *inst = (drv_input_inst_t *)malloc(sizeof(drv_input_inst_t));
    if (*inst == NULL) {
        close(fd);
        return -ENOMEM;
    }

    memset(*inst, 0, sizeof(drv_input_inst_t));
    (*inst)->base = (void *)&_drv_input_inst_type;
    (*inst)->id = id;
    (*inst)->fd = fd;
    strncpy((*inst)->path, path, sizeof((*inst)->path) - 1);
    (*inst)->path[sizeof((*inst)->path) - 1] = '\0';

    return 0;
}

static uint32_t drv_input_button_mask(uint16_t code)
{
    switch (code) {
    case BTN_LEFT:
        return 1u << 0;
    case BTN_RIGHT:
        return 1u << 1;
    case BTN_MIDDLE:
        return 1u << 2;
    case BTN_SIDE:
        return 1u << 3;
    case BTN_EXTRA:
        return 1u << 4;
    case BTN_FORWARD:
        return 1u << 5;
    case BTN_BACK:
        return 1u << 6;
    case BTN_TOUCH:
        return 1u << 7;
    default:
        return 0;
    }
}

int drv_input_inst_create(int id, drv_input_inst_t **inst)
{
    char path[DRV_INPUT_PATH_MAX];

    if (id < 0 || inst == NULL) {
        return -EINVAL;
    }

    snprintf(path, sizeof(path), "/dev/input/event%d", id);
    path[sizeof(path) - 1] = '\0';
    return drv_input_open_path(path, inst, id);
}

int drv_input_inst_create_path(const char *path, drv_input_inst_t **inst)
{
    return drv_input_open_path(path, inst, -1);
}

void drv_input_inst_destroy(drv_input_inst_t **inst)
{
    if (inst == NULL || *inst == NULL) {
        return;
    }

    if ((*inst)->base != (void *)&_drv_input_inst_type) {
        return;
    }

    if ((*inst)->fd >= 0) {
        close((*inst)->fd);
    }

    free(*inst);
    *inst = NULL;
}

int drv_input_poll(drv_input_inst_t *inst, int timeout_ms)
{
    int remaining_ms;
    int ret;

    if (inst == NULL || inst->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (timeout_ms == 0) {
        ret = drv_input_poll_once(inst, 0);
        if (ret == 0 && drv_input_path_rebound(inst)) {
            return -ENODEV;
        }
        return ret;
    }

    remaining_ms = timeout_ms;
    while (timeout_ms < 0 || remaining_ms > 0) {
        int slice_ms;

        if (timeout_ms < 0) {
            slice_ms = DRV_INPUT_POLL_SLICE_MS;
        } else if (remaining_ms > DRV_INPUT_POLL_SLICE_MS) {
            slice_ms = DRV_INPUT_POLL_SLICE_MS;
        } else {
            slice_ms = remaining_ms;
        }

        ret = drv_input_poll_once(inst, slice_ms);
        if (ret != 0) {
            return ret;
        }

        if (drv_input_path_rebound(inst)) {
            return -ENODEV;
        }

        if (timeout_ms > 0) {
            remaining_ms -= slice_ms;
        }
    }

    return 0;
}

int drv_input_read_event(drv_input_inst_t *inst, struct input_event *event)
{
    ssize_t bytes_read;

    if (inst == NULL || inst->fd < 0 || event == NULL) {
        return -EINVAL;
    }

    bytes_read = read(inst->fd, event, sizeof(*event));
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -errno;
    }

    if (bytes_read == 0) {
        return -ENODEV;
    }

    if ((size_t)bytes_read != sizeof(*event)) {
        return -EIO;
    }

    return 1;
}

int drv_input_read_frame(drv_input_inst_t *inst, struct drv_input_frame *frame)
{
    int ret;

    if (inst == NULL || frame == NULL) {
        return -1;
    }

    memset(frame, 0, sizeof(*frame));

    while (frame->count < DRV_INPUT_MAX_FRAME_EVENTS) {
        ret = drv_input_read_event(inst, &frame->events[frame->count]);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            return (int)frame->count;
        }

        if (drv_input_is_sync_event(&frame->events[frame->count])) {
            frame->complete = true;
            return (int)frame->count;
        }

        frame->count++;
    }

    return (int)frame->count;
}

int drv_input_read_keyboard_frame(drv_input_inst_t *inst, struct drv_keyboard_frame *frame)
{
    struct drv_input_frame raw_frame;
    size_t index;
    int ret;

    if (frame == NULL) {
        return -1;
    }

    memset(frame, 0, sizeof(*frame));

    ret = drv_input_read_frame(inst, &raw_frame);
    if (ret < 0) {
        return ret;
    }

    frame->complete = raw_frame.complete;
    for (index = 0; index < raw_frame.count; index++) {
        const struct input_event *event = &raw_frame.events[index];

        if (!drv_input_is_key_event(event)) {
            continue;
        }

        if (frame->count >= DRV_INPUT_MAX_FRAME_EVENTS) {
            break;
        }

        frame->keycodes[frame->count] = event->code;
        frame->values[frame->count] = event->value;
        frame->count++;
    }

    return (int)frame->count;
}

int drv_input_read_pointer_frame(drv_input_inst_t *inst, struct drv_pointer_frame *frame)
{
    struct drv_input_frame raw_frame;
    size_t index;
    int ret;

    if (inst == NULL || frame == NULL) {
        return -1;
    }

    memset(frame, 0, sizeof(*frame));

    ret = drv_input_read_frame(inst, &raw_frame);
    if (ret < 0) {
        return ret;
    }

    frame->complete = raw_frame.complete;

    for (index = 0; index < raw_frame.count; index++) {
        const struct input_event *event = &raw_frame.events[index];
        uint32_t mask;

        if (drv_input_is_rel_event(event)) {
            frame->has_rel = true;
            switch (event->code) {
            case REL_X:
                frame->rel_x += event->value;
                break;
            case REL_Y:
                frame->rel_y += event->value;
                break;
            case REL_WHEEL:
                frame->wheel += event->value;
                break;
            case REL_HWHEEL:
                frame->hwheel += event->value;
                break;
            default:
                break;
            }
            continue;
        }

        if (drv_input_is_abs_event(event)) {
            frame->has_abs = true;
            switch (event->code) {
            case ABS_X:
                frame->abs_x = event->value;
                break;
            case ABS_Y:
                frame->abs_y = event->value;
                break;
            case ABS_PRESSURE:
                frame->pressure = event->value;
                break;
            default:
                break;
            }
            continue;
        }

        if (!drv_input_is_key_event(event)) {
            continue;
        }

        mask = drv_input_button_mask(event->code);
        if (mask == 0) {
            continue;
        }

        if (event->value == KEY_PRESSED) {
            inst->button_state |= mask;
            frame->pressed_mask |= mask;
        } else if (event->value == KEY_RELEASED) {
            inst->button_state &= ~mask;
            frame->released_mask |= mask;
        }

        if (event->code == BTN_TOUCH) {
            frame->touch_seen = true;
            frame->touch_down = (event->value == KEY_PRESSED);
        }
    }

    frame->buttons = inst->button_state;
    return (int)raw_frame.count;
}

int drv_input_get_info(drv_input_inst_t *inst, struct drv_input_info *info)
{
    int ret;

    if (inst == NULL || inst->fd < 0 || info == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(info, 0, sizeof(*info));
    ret = ioctl(inst->fd, DRV_INPUT_CTRL_GET_INFO, info);
    if (ret < 0) {
        return -errno;
    }

    drv_input_cache_info(inst, info);

    return 0;
}

int drv_input_find_first_by_type(uint32_t kind, char *path, size_t path_size,
                                 struct drv_input_info *info)
{
    int id;

    if (path == NULL || path_size == 0) {
        errno = EINVAL;
        return -1;
    }

    for (id = 0; id < 32; id++) {
        drv_input_inst_t *inst = NULL;
        struct drv_input_info local_info;

        if (drv_input_inst_create(id, &inst) != 0)
            continue;

        if (drv_input_get_info(inst, &local_info) == 0 && local_info.kind == kind) {
            snprintf(path, path_size, "/dev/input/event%d", id);
            path[path_size - 1] = '\0';
            if (info != NULL)
                *info = local_info;
            drv_input_inst_destroy(&inst);
            return 0;
        }

        drv_input_inst_destroy(&inst);
    }

    return -ENOENT;
}

bool drv_input_is_disconnect_error(int ret)
{
    return drv_input_ret_is_disconnect(ret);
}

int drv_input_reconnect_path(drv_input_inst_t **inst, const char *path, struct drv_input_info *info)
{
    drv_input_inst_t *new_inst = NULL;
    int ret;

    if (inst == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    ret = drv_input_inst_create_path(path, &new_inst);
    if (ret != 0) {
        return ret;
    }

    if (info != NULL) {
        ret = drv_input_get_info(new_inst, info);
        if (ret != 0) {
            drv_input_inst_destroy(&new_inst);
            return ret;
        }
    }

    drv_input_inst_destroy(inst);
    *inst = new_inst;
    return 0;
}

int drv_input_reconnect_by_type(drv_input_inst_t **inst,
                                uint32_t kind,
                                char *path,
                                size_t path_size,
                                struct drv_input_info *info)
{
    struct drv_input_info local_info;
    char local_path[DRV_INPUT_PATH_MAX];
    int ret;

    if (path == NULL) {
        path = local_path;
        path_size = sizeof(local_path);
    }

    ret = drv_input_find_first_by_type(kind, path, path_size, info != NULL ? info : &local_info);
    if (ret != 0) {
        return ret;
    }

    return drv_input_reconnect_path(inst, path, info);
}

bool drv_input_is_key_event(const struct input_event *event)
{
    return event != NULL && event->type == EV_KEY;
}

bool drv_input_is_rel_event(const struct input_event *event)
{
    return event != NULL && event->type == EV_REL;
}

bool drv_input_is_abs_event(const struct input_event *event)
{
    return event != NULL && event->type == EV_ABS;
}

bool drv_input_is_sync_event(const struct input_event *event)
{
    return event != NULL && event->type == EV_SYN && event->code == SYN_REPORT;
}
