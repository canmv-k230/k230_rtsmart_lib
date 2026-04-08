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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "input_event.h"

#define DRV_INPUT_PATH_MAX         (64)
#define DRV_INPUT_MAX_FRAME_EVENTS (32)
#define DRV_INPUT_NAME_MAX         (32)

enum drv_input_device_type {
    DRV_INPUT_DEV_UNKNOWN = 0,
    DRV_INPUT_DEV_KEYBOARD,
    DRV_INPUT_DEV_MOUSE,
    DRV_INPUT_DEV_TOUCH,
};

struct drv_input_info {
    uint32_t kind;
    uint32_t ev_bits;
    uint32_t key_bits;
    uint32_t rel_bits;
    uint32_t abs_bits;
    char name[DRV_INPUT_NAME_MAX];
};

#define DRV_INPUT_CTRL_GET_INFO    0x1001

struct drv_input_frame {
    struct input_event events[DRV_INPUT_MAX_FRAME_EVENTS];
    size_t count;
    bool complete;
};

struct drv_keyboard_frame {
    uint16_t keycodes[DRV_INPUT_MAX_FRAME_EVENTS];
    int32_t values[DRV_INPUT_MAX_FRAME_EVENTS];
    size_t count;
    bool complete;
};

struct drv_pointer_frame {
    bool complete;
    bool has_rel;
    bool has_abs;
    bool touch_seen;
    bool touch_down;
    int32_t rel_x;
    int32_t rel_y;
    int32_t wheel;
    int32_t hwheel;
    int32_t abs_x;
    int32_t abs_y;
    int32_t pressure;
    uint32_t buttons;
    uint32_t pressed_mask;
    uint32_t released_mask;
};

typedef struct _drv_input_inst {
    void *base;
    int id;
    int fd;
    uint32_t button_state;
    struct drv_input_info info;
    char path[DRV_INPUT_PATH_MAX];
    uint32_t preferred_kind;
    bool auto_reconnect;
} drv_input_inst_t;

int drv_input_inst_create(int id, drv_input_inst_t **inst);
int drv_input_inst_create_path(const char *path, drv_input_inst_t **inst);
void drv_input_inst_destroy(drv_input_inst_t **inst);
void drv_input_inst_mark_disconnected(drv_input_inst_t *inst);
bool drv_input_inst_is_connected(drv_input_inst_t *inst);
int drv_input_inst_try_reconnect(drv_input_inst_t *inst);
void drv_input_inst_set_auto_reconnect(drv_input_inst_t *inst, uint32_t kind);

int drv_input_poll(drv_input_inst_t *inst, int timeout_ms);
int drv_input_read_event(drv_input_inst_t *inst, struct input_event *event);
int drv_input_read_frame(drv_input_inst_t *inst, struct drv_input_frame *frame);
int drv_input_read_keyboard_frame(drv_input_inst_t *inst, struct drv_keyboard_frame *frame);
int drv_input_read_pointer_frame(drv_input_inst_t *inst, struct drv_pointer_frame *frame);
int drv_input_get_info(drv_input_inst_t *inst, struct drv_input_info *info);
int drv_input_find_first_by_type(uint32_t kind, char *path, size_t path_size, struct drv_input_info *info);
bool drv_input_is_disconnect_error(int ret);

bool drv_input_is_key_event(const struct input_event *event);
bool drv_input_is_rel_event(const struct input_event *event);
bool drv_input_is_abs_event(const struct input_event *event);
bool drv_input_is_sync_event(const struct input_event *event);

#ifdef __cplusplus
}
#endif