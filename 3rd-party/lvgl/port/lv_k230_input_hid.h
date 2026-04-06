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

#include "src/indev/lv_indev.h"

/**
 * @brief Initialize an LVGL pointer input device from a HID event path.
 * @param dev_path Input event device path, for example /dev/input/event1.
 * @return Pointer to the created LVGL input device on success, NULL on failure.
 */
lv_indev_t* lv_k230_hid_pointer_init_path(const char* dev_path);

/**
 * @brief Auto-detect the first HID mouse device and wrap it as an LVGL pointer.
 * @return Pointer to the created LVGL input device on success, NULL on failure.
 */
lv_indev_t* lv_k230_hid_pointer_init_auto(void);

/**
 * @brief Enable or disable automatic reconnect for a HID-backed LVGL input device.
 * @param indev LVGL input device created by this wrapper.
 * @param enabled true to rescan and reconnect after unplug, false to keep current behavior.
 */
void lv_k230_hid_set_auto_reconnect(lv_indev_t* indev, bool enabled);

/**
 * @brief Initialize an LVGL keypad input device from a HID event path.
 * @param dev_path Input event device path, for example /dev/input/event0.
 * @return Pointer to the created LVGL input device on success, NULL on failure.
 */
lv_indev_t* lv_k230_hid_keypad_init_path(const char* dev_path);

/**
 * @brief Auto-detect the first HID keyboard device and wrap it as an LVGL keypad.
 * @return Pointer to the created LVGL input device on success, NULL on failure.
 */
lv_indev_t* lv_k230_hid_keypad_init_auto(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif