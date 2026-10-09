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
#include "lv_k230_input_touch.h"

#include <stdio.h>
#include <stdlib.h>

#include "mpi_vo_api.h"
#include "src/display/lv_display.h"

typedef struct {
    drv_touch_inst_t* touch_inst;
    k_gdma_rotation_e display_rotation;
} lv_k230_touch_ctx_t;

static void lv_k230_touch_transform_point(lv_indev_t* indev, k_gdma_rotation_e rotation,
                                          lv_point_t* point)
{
    lv_display_t* display = lv_indev_get_display(indev);
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    if (display == NULL || point == NULL) {
        return;
    }

    x = point->x;
    y = point->y;
    width = lv_display_get_horizontal_resolution(display);
    height = lv_display_get_vertical_resolution(display);

    switch (rotation) {
    case GDMA_ROTATE_DEGREE_90:
        point->x = y;
        point->y = height - x - 1;
        break;
    case GDMA_ROTATE_DEGREE_180:
        point->x = width - x - 1;
        point->y = height - y - 1;
        break;
    case GDMA_ROTATE_DEGREE_270:
        point->x = width - y - 1;
        point->y = x;
        break;
    default:
        break;
    }
}

/**
 * @brief Touch input device read callback
 * @param indev Pointer to input device
 * @param data Pointer to store input data
 */
static void lv_k230_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    struct drv_touch_data touch_data[DRV_TOUCH_POINT_NUMBER_MAX];
    int                   points_read;

    lv_k230_touch_ctx_t* ctx = (lv_k230_touch_ctx_t*)lv_indev_get_driver_data(indev);

    if (ctx == NULL || ctx->touch_inst == NULL) {
        printf("invalid touch instance\n");
        return;
    }

    /* Initialize data structure */
    data->state            = LV_INDEV_STATE_RELEASED;
    data->point.x          = 0;
    data->point.y          = 0;
    data->timestamp        = lv_tick_get();
    data->continue_reading = false;

    /* Read touch data from driver */
    points_read = drv_touch_read(ctx->touch_inst, touch_data, DRV_TOUCH_POINT_NUMBER_MAX);

    if (points_read < 0) {
        /* Read error, keep released state */
        return;
    }

    if (points_read > 0) {
        /* Process first touch point (multi-touch not fully supported yet) */
        struct drv_touch_data* point = &touch_data[0];

        /* Convert touch event to LVGL state */
        switch (point->event) {
        case DRV_TOUCH_EVENT_DOWN:
        case DRV_TOUCH_EVENT_MOVE:
            data->state = LV_INDEV_STATE_PRESSED;
            break;
        case DRV_TOUCH_EVENT_UP:
        case DRV_TOUCH_EVENT_NONE:
        default:
            data->state = LV_INDEV_STATE_RELEASED;
            break;
        }

        /* Set coordinates */
        data->point.x = point->x_coordinate;
        data->point.y = point->y_coordinate;
        lv_k230_touch_transform_point(indev, ctx->display_rotation, &data->point);

        /* Use timestamp from touch driver if available */
        if (point->timestamp != 0) {
            data->timestamp = point->timestamp;
        }

        // /* Continue reading if there are more points */
        // if (points_read > 1) {
        //     data->continue_reading = true;
        // }
    }
}

static void event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = (lv_indev_t*)lv_event_get_target(e);
    lv_k230_touch_ctx_t* ctx = (lv_k230_touch_ctx_t*)lv_indev_get_driver_data(indev);

    switch (code) {
    case LV_EVENT_DELETE: {
        if (ctx != NULL) {
            printf("destroy touch instance\n");
            drv_touch_inst_destroy(&ctx->touch_inst);
            free(ctx);
            lv_indev_set_driver_data(indev, NULL);
        }
    } break;
    default: {
        printf("unsupport event %d\n", code);
    } break;
    }
}

/**
 * @brief Initialize LVGL touch input device
 * @param touch_id Touch interface ID (0 to KD_HARD_TOUCH_MAX_NUM-1)
 * @return Pointer to created input device on success, NULL on failure
 */
lv_indev_t* lv_k230_touch_init(int touch_id)
{
    lv_indev_t* indev = NULL;
    drv_touch_inst_t* inst = NULL;
    lv_k230_touch_ctx_t* ctx = NULL;
    k_vo_dev_attr dev_attr;
    int ret;

    /* Create touch driver instance */
    ret = drv_touch_inst_create(touch_id, &inst);
    if (ret != 0) {
        printf("[lv_touch] failed to create touch instance, ret=%d\n", ret);
        return NULL;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        drv_touch_inst_destroy(&inst);
        return NULL;
    }
    ctx->touch_inst = inst;

    if (kd_mpi_vo_get_dev_attr(&dev_attr) == K_SUCCESS) {
        ctx->display_rotation = dev_attr.dev_rot_flg;
    }

    /* Create LVGL input device */
    indev = lv_indev_create();
    if (indev == NULL) {
        printf("[lv_touch] failed to create LVGL input device\n");
        drv_touch_inst_destroy(&ctx->touch_inst);
        free(ctx);
        return NULL;
    }

    /* Set input device type to touch/pointer */
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);

    /* Set read callback */
    lv_indev_set_read_cb(indev, lv_k230_touch_read_cb);

    lv_indev_set_driver_data(indev, ctx);

    lv_indev_add_event_cb(indev, event_cb, LV_EVENT_DELETE, NULL);

    printf("[lv_touch] touch input device initialized successfully\n");

    return indev;
}
