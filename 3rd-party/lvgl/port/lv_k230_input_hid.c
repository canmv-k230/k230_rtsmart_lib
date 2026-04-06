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

#include "lv_k230_input_hid.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drv_input.h"
#include "lvgl.h"

/* Keep these prototypes local so this port still builds cleanly if the HAL header
 * is picked up through a stale include path by editor tooling or older build glue.
 */
bool drv_input_is_disconnect_error(int ret);
int drv_input_reconnect_by_type(drv_input_inst_t **inst,
                                uint32_t kind,
                                char *path,
                                size_t path_size,
                                struct drv_input_info *info);

#define LV_K230_HID_KEY_QUEUE_SIZE   32
#define LV_K230_HID_MAX_READ_BATCH   16
#define LV_K230_HID_TRANSLATED_KEYS  128
#define LV_K230_HID_BTN_LEFT_MASK    (1u << 0)
#define LV_K230_HID_BTN_TOUCH_MASK   (1u << 7)
#define LV_K230_HID_RECONNECT_MS     500

typedef struct {
    uint32_t key;
    lv_indev_state_t state;
    uint32_t timestamp;
} lv_k230_hid_key_event_t;

typedef struct {
    drv_input_inst_t* inst;
    struct drv_input_info info;
    uint32_t preferred_kind;
    uint32_t reconnect_after;
    bool auto_reconnect;
    lv_point_t point;
    bool point_initialized;
    lv_indev_state_t pointer_state;
    uint32_t active_key;
    lv_indev_state_t active_key_state;
    bool shift_pressed;
    bool caps_lock_enabled;
    uint32_t translated_keys[LV_K230_HID_TRANSLATED_KEYS];
    lv_k230_hid_key_event_t key_events[LV_K230_HID_KEY_QUEUE_SIZE];
    uint8_t key_event_count;
} lv_k230_hid_ctx_t;

static void lv_k230_hid_destroy_ctx(lv_k230_hid_ctx_t** ctx)
{
    if (ctx == NULL || *ctx == NULL) {
        return;
    }

    drv_input_inst_destroy(&(*ctx)->inst);
    lv_free(*ctx);
    *ctx = NULL;
}

static bool lv_k230_hid_pointer_kind_ok(uint32_t kind)
{
    return kind == DRV_INPUT_DEV_MOUSE || kind == DRV_INPUT_DEV_TOUCH;
}

static bool lv_k230_hid_keypad_kind_ok(uint32_t kind)
{
    return kind == DRV_INPUT_DEV_KEYBOARD;
}

static lv_k230_hid_ctx_t* lv_k230_hid_open_ctx(const char* dev_path)
{
    lv_k230_hid_ctx_t* ctx;

    if (dev_path == NULL) {
        return NULL;
    }

    ctx = lv_malloc_zeroed(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    if (drv_input_inst_create_path(dev_path, &ctx->inst) != 0) {
        lv_free(ctx);
        printf("[lv_hid] failed to open %s\n", dev_path);
        return NULL;
    }

    if (drv_input_get_info(ctx->inst, &ctx->info) != 0) {
        printf("[lv_hid] failed to query %s info\n", dev_path);
        lv_k230_hid_destroy_ctx(&ctx);
        return NULL;
    }

    ctx->active_key_state = LV_INDEV_STATE_RELEASED;
    ctx->pointer_state = LV_INDEV_STATE_RELEASED;
    return ctx;
}

static void lv_k230_hid_reset_runtime_state(lv_k230_hid_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->point_initialized = false;
    ctx->pointer_state = LV_INDEV_STATE_RELEASED;
    ctx->active_key = 0;
    ctx->active_key_state = LV_INDEV_STATE_RELEASED;
    ctx->shift_pressed = false;
    ctx->caps_lock_enabled = false;
    ctx->key_event_count = 0;
    memset(ctx->translated_keys, 0, sizeof(ctx->translated_keys));
    memset(ctx->key_events, 0, sizeof(ctx->key_events));
}

static bool lv_k230_hid_try_reconnect(lv_k230_hid_ctx_t* ctx)
{
    uint32_t now;
    int ret;

    if (ctx == NULL || !ctx->auto_reconnect || ctx->preferred_kind == DRV_INPUT_DEV_UNKNOWN) {
        return false;
    }

    now = lv_tick_get();
    if ((int32_t)(now - ctx->reconnect_after) < 0) {
        return false;
    }

    ret = drv_input_reconnect_by_type(&ctx->inst,
                                      ctx->preferred_kind,
                                      ctx->inst != NULL ? ctx->inst->path : NULL,
                                      ctx->inst != NULL ? sizeof(ctx->inst->path) : 0,
                                      &ctx->info);
    if (ret == 0) {
        lv_k230_hid_reset_runtime_state(ctx);
        printf("[lv_hid] reconnected %s on %s\n", ctx->info.name, ctx->inst->path);
        return true;
    }

    ctx->reconnect_after = now + LV_K230_HID_RECONNECT_MS;
    return false;
}

static void lv_k230_hid_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = (lv_indev_t*)lv_event_get_target(e);
    lv_k230_hid_ctx_t* ctx = (lv_k230_hid_ctx_t*)lv_indev_get_driver_data(indev);

    if (code == LV_EVENT_DELETE && ctx != NULL) {
        lv_k230_hid_destroy_ctx(&ctx);
    }
}

static void lv_k230_hid_init_pointer_position(lv_indev_t* indev, lv_k230_hid_ctx_t* ctx)
{
    lv_display_t* display;
    int32_t width;
    int32_t height;

    if (ctx == NULL || ctx->point_initialized) {
        return;
    }

    display = lv_indev_get_display(indev);
    if (display == NULL) {
        return;
    }

    width = lv_display_get_horizontal_resolution(display);
    height = lv_display_get_vertical_resolution(display);

    ctx->point.x = width > 0 ? width / 2 : 0;
    ctx->point.y = height > 0 ? height / 2 : 0;
    ctx->point_initialized = true;
}

static void lv_k230_hid_clamp_point(lv_indev_t* indev, lv_point_t* point)
{
    lv_display_t* display;
    int32_t width;
    int32_t height;

    if (point == NULL) {
        return;
    }

    display = lv_indev_get_display(indev);
    if (display == NULL) {
        return;
    }

    width = lv_display_get_horizontal_resolution(display);
    height = lv_display_get_vertical_resolution(display);

    if (width > 0) {
        if (point->x < 0) {
            point->x = 0;
        } else if (point->x >= width) {
            point->x = width - 1;
        }
    }

    if (height > 0) {
        if (point->y < 0) {
            point->y = 0;
        } else if (point->y >= height) {
            point->y = height - 1;
        }
    }
}

static bool lv_k230_hid_enqueue_key_event(lv_k230_hid_ctx_t* ctx, uint32_t key,
                                          lv_indev_state_t state, uint32_t timestamp)
{
    if (ctx == NULL || key == 0) {
        return false;
    }

    if (ctx->key_event_count >= LV_K230_HID_KEY_QUEUE_SIZE) {
        memmove(&ctx->key_events[0],
                &ctx->key_events[1],
                sizeof(ctx->key_events[0]) * (LV_K230_HID_KEY_QUEUE_SIZE - 1));
        ctx->key_event_count = LV_K230_HID_KEY_QUEUE_SIZE - 1;
    }

    ctx->key_events[ctx->key_event_count].key = key;
    ctx->key_events[ctx->key_event_count].state = state;
    ctx->key_events[ctx->key_event_count].timestamp = timestamp;
    ctx->key_event_count++;
    return true;
}

static bool lv_k230_hid_is_shift_key(uint16_t code)
{
    return code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT;
}

static uint32_t lv_k230_hid_translate_alpha_key(bool shifted, uint16_t code)
{
    switch (code) {
    case KEY_A: return shifted ? 'A' : 'a';
    case KEY_B: return shifted ? 'B' : 'b';
    case KEY_C: return shifted ? 'C' : 'c';
    case KEY_D: return shifted ? 'D' : 'd';
    case KEY_E: return shifted ? 'E' : 'e';
    case KEY_F: return shifted ? 'F' : 'f';
    case KEY_G: return shifted ? 'G' : 'g';
    case KEY_H: return shifted ? 'H' : 'h';
    case KEY_I: return shifted ? 'I' : 'i';
    case KEY_J: return shifted ? 'J' : 'j';
    case KEY_K: return shifted ? 'K' : 'k';
    case KEY_L: return shifted ? 'L' : 'l';
    case KEY_M: return shifted ? 'M' : 'm';
    case KEY_N: return shifted ? 'N' : 'n';
    case KEY_O: return shifted ? 'O' : 'o';
    case KEY_P: return shifted ? 'P' : 'p';
    case KEY_Q: return shifted ? 'Q' : 'q';
    case KEY_R: return shifted ? 'R' : 'r';
    case KEY_S: return shifted ? 'S' : 's';
    case KEY_T: return shifted ? 'T' : 't';
    case KEY_U: return shifted ? 'U' : 'u';
    case KEY_V: return shifted ? 'V' : 'v';
    case KEY_W: return shifted ? 'W' : 'w';
    case KEY_X: return shifted ? 'X' : 'x';
    case KEY_Y: return shifted ? 'Y' : 'y';
    case KEY_Z: return shifted ? 'Z' : 'z';
    default:
        return 0;
    }
}

static uint32_t lv_k230_hid_translate_ascii_key(lv_k230_hid_ctx_t* ctx, uint16_t code)
{
    uint32_t alpha_key;
    bool shifted;

    if (ctx == NULL) {
        return 0;
    }

    shifted = (ctx->shift_pressed != ctx->caps_lock_enabled);

    alpha_key = lv_k230_hid_translate_alpha_key(shifted, code);
    if (alpha_key != 0) {
        return alpha_key;
    }

    switch (code) {
    case KEY_1: return shifted ? '!' : '1';
    case KEY_2: return shifted ? '@' : '2';
    case KEY_3: return shifted ? '#' : '3';
    case KEY_4: return shifted ? '$' : '4';
    case KEY_5: return shifted ? '%' : '5';
    case KEY_6: return shifted ? '^' : '6';
    case KEY_7: return shifted ? '&' : '7';
    case KEY_8: return shifted ? '*' : '8';
    case KEY_9: return shifted ? '(' : '9';
    case KEY_0: return shifted ? ')' : '0';
    case KEY_SPACE: return ' ';
    case KEY_MINUS: return shifted ? '_' : '-';
    case KEY_EQUAL: return shifted ? '+' : '=';
    case KEY_LEFTBRACE: return shifted ? '{' : '[';
    case KEY_RIGHTBRACE: return shifted ? '}' : ']';
    case KEY_BACKSLASH: return shifted ? '|' : '\\';
    case KEY_SEMICOLON: return shifted ? ':' : ';';
    case KEY_APOSTROPHE: return shifted ? '"' : '\'';
    case KEY_GRAVE: return shifted ? '~' : '`';
    case KEY_COMMA: return shifted ? '<' : ',';
    case KEY_DOT: return shifted ? '>' : '.';
    case KEY_SLASH: return shifted ? '?' : '/';
    case KEY_KP0: return '0';
    case KEY_KP1: return '1';
    case KEY_KP2: return '2';
    case KEY_KP3: return '3';
    case KEY_KP4: return '4';
    case KEY_KP5: return '5';
    case KEY_KP6: return '6';
    case KEY_KP7: return '7';
    case KEY_KP8: return '8';
    case KEY_KP9: return '9';
    case KEY_KPDOT: return '.';
    case KEY_KPPLUS: return '+';
    case KEY_KPMINUS: return '-';
    case KEY_KPSLASH: return '/';
    case KEY_KPASTERISK: return '*';
    default:
        return 0;
    }
}

static uint32_t lv_k230_hid_translate_special_key(lv_k230_hid_ctx_t* ctx, uint16_t code)
{
    bool shifted = ctx != NULL ? ctx->shift_pressed : false;

    switch (code) {
    case KEY_ENTER:
    case KEY_KPENTER:
        return LV_KEY_ENTER;
    case KEY_ESC:
        return LV_KEY_ESC;
    case KEY_BACKSPACE:
        return LV_KEY_BACKSPACE;
    case KEY_DELETE:
        return LV_KEY_DEL;
    case KEY_TAB:
        return shifted ? LV_KEY_PREV : LV_KEY_NEXT;
    case KEY_UP:
        return LV_KEY_UP;
    case KEY_DOWN:
        return LV_KEY_DOWN;
    case KEY_LEFT:
        return LV_KEY_LEFT;
    case KEY_RIGHT:
        return LV_KEY_RIGHT;
    case KEY_HOME:
        return LV_KEY_HOME;
    case KEY_END:
        return LV_KEY_END;
    case KEY_PAGEUP:
        return LV_KEY_PREV;
    case KEY_PAGEDOWN:
        return LV_KEY_NEXT;
    default:
        return 0;
    }
}

static uint32_t lv_k230_hid_translate_keycode(lv_k230_hid_ctx_t* ctx, uint16_t code)
{
    uint32_t key = lv_k230_hid_translate_special_key(ctx, code);

    if (key != 0) {
        return key;
    }

    return lv_k230_hid_translate_ascii_key(ctx, code);
}

static void lv_k230_hid_pointer_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    struct drv_pointer_frame frame;
    lv_k230_hid_ctx_t* ctx = (lv_k230_hid_ctx_t*)lv_indev_get_driver_data(indev);
    int ret;

    if (ctx == NULL) {
        return;
    }

    lv_k230_hid_init_pointer_position(indev, ctx);

    data->state = ctx->pointer_state;
    data->point = ctx->point;
    data->timestamp = lv_tick_get();
    data->continue_reading = false;

    ret = drv_input_read_pointer_frame(ctx->inst, &frame);
    if (ret > 0) {
        if (frame.has_abs) {
            ctx->point.x = frame.abs_x;
            ctx->point.y = frame.abs_y;
            ctx->point_initialized = true;
        }

        if (frame.has_rel) {
            ctx->point.x += frame.rel_x;
            ctx->point.y += frame.rel_y;
            ctx->point_initialized = true;
        }

        ctx->pointer_state = (frame.buttons & (LV_K230_HID_BTN_LEFT_MASK | LV_K230_HID_BTN_TOUCH_MASK)) != 0
                                 ? LV_INDEV_STATE_PRESSED
                                 : LV_INDEV_STATE_RELEASED;
        data->timestamp = lv_tick_get();
        if (drv_input_poll(ctx->inst, 0) > 0) {
            data->continue_reading = true;
        }
    } else if (drv_input_is_disconnect_error(ret)) {
        lv_k230_hid_try_reconnect(ctx);
    }

    lv_k230_hid_clamp_point(indev, &ctx->point);
    data->state = ctx->pointer_state;
    data->point = ctx->point;
}

static void lv_k230_hid_collect_key_events(lv_k230_hid_ctx_t* ctx)
{
    struct drv_keyboard_frame frame;
    int reads = 0;

    if (ctx == NULL) {
        return;
    }

    while (ctx->key_event_count < LV_K230_HID_KEY_QUEUE_SIZE && reads < LV_K230_HID_MAX_READ_BATCH) {
        size_t index;
        int ret = drv_input_read_keyboard_frame(ctx->inst, &frame);

        if (ret <= 0) {
            if (drv_input_is_disconnect_error(ret)) {
                lv_k230_hid_try_reconnect(ctx);
            }
            break;
        }

        for (index = 0; index < frame.count; index++) {
            uint16_t code = frame.keycodes[index];
            int32_t value = frame.values[index];
            uint32_t key;

            if (lv_k230_hid_is_shift_key(code)) {
                if (value == KEY_RELEASED) {
                    ctx->shift_pressed = false;
                } else {
                    ctx->shift_pressed = true;
                }
                continue;
            }

            if (code == KEY_CAPSLOCK && value == KEY_PRESSED) {
                ctx->caps_lock_enabled = !ctx->caps_lock_enabled;
                continue;
            }

            if (value == KEY_RELEASED && code < LV_K230_HID_TRANSLATED_KEYS && ctx->translated_keys[code] != 0) {
                key = ctx->translated_keys[code];
                ctx->translated_keys[code] = 0;
            } else {
                key = lv_k230_hid_translate_keycode(ctx, code);
                if (value != KEY_RELEASED && code < LV_K230_HID_TRANSLATED_KEYS) {
                    ctx->translated_keys[code] = key;
                }
            }

            if (key == 0) {
                continue;
            }

            lv_k230_hid_enqueue_key_event(ctx,
                                          key,
                                          value == KEY_RELEASED ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED,
                                          lv_tick_get());
        }

        reads++;
    }
}

static void lv_k230_hid_keypad_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    lv_k230_hid_ctx_t* ctx = (lv_k230_hid_ctx_t*)lv_indev_get_driver_data(indev);

    if (ctx == NULL) {
        return;
    }

    if (ctx->key_event_count == 0) {
        lv_k230_hid_collect_key_events(ctx);
    }

    data->timestamp = lv_tick_get();
    data->continue_reading = false;
    data->key = ctx->active_key;
    data->state = ctx->active_key_state;

    if (ctx->key_event_count > 0) {
        lv_k230_hid_key_event_t event = ctx->key_events[0];

        if (ctx->key_event_count > 1) {
            memmove(&ctx->key_events[0],
                    &ctx->key_events[1],
                    sizeof(ctx->key_events[0]) * (ctx->key_event_count - 1));
        }

        ctx->key_event_count--;
        ctx->active_key = event.key;
        ctx->active_key_state = event.state;

        data->key = event.key;
        data->state = event.state;
        data->timestamp = event.timestamp;
        data->continue_reading = ctx->key_event_count > 0;
    }
}

static lv_indev_t* lv_k230_hid_create_pointer(const char* dev_path)
{
    lv_indev_t* indev;
    lv_k230_hid_ctx_t* ctx = lv_k230_hid_open_ctx(dev_path);

    if (ctx == NULL) {
        return NULL;
    }

    if (!lv_k230_hid_pointer_kind_ok(ctx->info.kind)) {
        printf("[lv_hid] %s is not a pointer device (%s)\n", dev_path, ctx->info.name);
        lv_k230_hid_destroy_ctx(&ctx);
        return NULL;
    }

    ctx->preferred_kind = ctx->info.kind == DRV_INPUT_DEV_TOUCH ? DRV_INPUT_DEV_TOUCH : DRV_INPUT_DEV_MOUSE;
    ctx->auto_reconnect = true;
    ctx->reconnect_after = 0;

    indev = lv_indev_create();
    if (indev == NULL) {
        lv_k230_hid_destroy_ctx(&ctx);
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lv_k230_hid_pointer_read_cb);
    lv_indev_set_driver_data(indev, ctx);
    lv_indev_add_event_cb(indev, lv_k230_hid_event_cb, LV_EVENT_DELETE, NULL);

    printf("[lv_hid] pointer input initialized: %s (%s)\n", dev_path, ctx->info.name);
    return indev;
}

static lv_indev_t* lv_k230_hid_create_keypad(const char* dev_path)
{
    lv_indev_t* indev;
    lv_k230_hid_ctx_t* ctx = lv_k230_hid_open_ctx(dev_path);

    if (ctx == NULL) {
        return NULL;
    }

    if (!lv_k230_hid_keypad_kind_ok(ctx->info.kind)) {
        printf("[lv_hid] %s is not a keyboard device (%s)\n", dev_path, ctx->info.name);
        lv_k230_hid_destroy_ctx(&ctx);
        return NULL;
    }

    ctx->preferred_kind = DRV_INPUT_DEV_KEYBOARD;
    ctx->auto_reconnect = true;
    ctx->reconnect_after = 0;

    indev = lv_indev_create();
    if (indev == NULL) {
        lv_k230_hid_destroy_ctx(&ctx);
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lv_k230_hid_keypad_read_cb);
    lv_indev_set_driver_data(indev, ctx);
    lv_indev_add_event_cb(indev, lv_k230_hid_event_cb, LV_EVENT_DELETE, NULL);

    printf("[lv_hid] keypad input initialized: %s (%s)\n", dev_path, ctx->info.name);
    return indev;
}

lv_indev_t* lv_k230_hid_pointer_init_path(const char* dev_path)
{
    return lv_k230_hid_create_pointer(dev_path);
}

lv_indev_t* lv_k230_hid_pointer_init_auto(void)
{
    char path[DRV_INPUT_PATH_MAX];
    struct drv_input_info info;

    if (drv_input_find_first_by_type(DRV_INPUT_DEV_MOUSE, path, sizeof(path), &info) != 0) {
        printf("[lv_hid] no HID pointer device found\n");
        return NULL;
    }

    return lv_k230_hid_create_pointer(path);
}

void lv_k230_hid_set_auto_reconnect(lv_indev_t* indev, bool enabled)
{
    lv_k230_hid_ctx_t* ctx;

    if (indev == NULL) {
        return;
    }

    ctx = (lv_k230_hid_ctx_t*)lv_indev_get_driver_data(indev);
    if (ctx == NULL) {
        return;
    }

    ctx->auto_reconnect = enabled;
    if (enabled) {
        ctx->reconnect_after = 0;
    }
}

lv_indev_t* lv_k230_hid_keypad_init_path(const char* dev_path)
{
    return lv_k230_hid_create_keypad(dev_path);
}

lv_indev_t* lv_k230_hid_keypad_init_auto(void)
{
    char path[DRV_INPUT_PATH_MAX];
    struct drv_input_info info;

    if (drv_input_find_first_by_type(DRV_INPUT_DEV_KEYBOARD, path, sizeof(path), &info) != 0) {
        printf("[lv_hid] no HID keyboard device found\n");
        return NULL;
    }

    return lv_k230_hid_create_keypad(path);
}