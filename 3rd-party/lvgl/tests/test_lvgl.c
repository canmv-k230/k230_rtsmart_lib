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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <getopt.h>
#include <signal.h>

#include "../port/lv_k230_display.h"

#include "k_vb_comm.h"
#include "lvgl.h"
#include "mpi_vb_api.h"

#define MAX_DISPLATY_WIDTH  (1920)
#define MAX_DISPLATY_HEIGHT (1080)

// Global variables for signal handling
static volatile int  g_signal_received = 0;
static lv_display_t* g_display         = NULL;

// Signal handler for graceful shutdown
static void signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down...\n", signum);
    g_signal_received = 1;
}

static void create_demo_widgets(lv_obj_t* parent)
{
    // Get screen resolution for responsive design
    int32_t screen_width  = lv_display_get_horizontal_resolution(lv_display_get_default());
    int32_t screen_height = lv_display_get_vertical_resolution(lv_display_get_default());

    // Use the only available font in the configuration
    const lv_font_t* normal_font = &lv_font_montserrat_14;

    // Main container with responsive padding
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(95), lv_pct(95));
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Responsive padding based on screen size
    int32_t padding = screen_width < 480 ? 5 : (screen_width < 800 ? 10 : 15);
    lv_obj_set_style_pad_all(container, padding, 0);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 10, 0);

    // Resolution info header
    lv_obj_t* header = lv_obj_create(container);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 5, 0);

    lv_obj_t* resolution_label = lv_label_create(header);
    lv_label_set_text_fmt(resolution_label, "Screen: %dx%d", screen_width, screen_height);
    lv_obj_set_style_text_font(resolution_label, normal_font, 0);
    lv_obj_set_style_text_color(resolution_label, lv_color_hex(0x00ff00), 0);

    // Title section
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "LVGL Multi-Resolution Demo");
    lv_obj_set_style_text_font(title, normal_font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00aaff), 0);
    lv_obj_set_style_pad_bottom(title, 10, 0);

    // Button section with responsive layout
    lv_obj_t* button_row = lv_obj_create(container);
    lv_obj_set_size(button_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_row, 0, 0);
    lv_obj_set_style_pad_all(button_row, 5, 0);

    // Create responsive buttons
    const char* button_texts[]  = { "Button 1", "Button 2", "Button 3" };
    lv_color_t  button_colors[] = { lv_color_hex(0x0066cc), lv_color_hex(0xcc6600), lv_color_hex(0x00cc66) };

    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn   = lv_button_create(button_row);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, button_texts[i]);
        lv_obj_set_style_text_font(label, normal_font, 0);
        lv_obj_center(label);
        lv_obj_set_style_bg_color(btn, button_colors[i], 0);

        // Responsive button size
        int32_t btn_width  = screen_width < 480 ? 70 : (screen_width < 800 ? 90 : 110);
        int32_t btn_height = screen_width < 480 ? 25 : (screen_width < 800 ? 35 : 45);
        lv_obj_set_size(btn, btn_width, btn_height);
    }

    // Progress bar section
    lv_obj_t* progress_container = lv_obj_create(container);
    lv_obj_set_size(progress_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(progress_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(progress_container, 0, 0);
    lv_obj_set_style_pad_all(progress_container, 10, 0);

    lv_obj_t* progress_label = lv_label_create(progress_container);
    lv_label_set_text(progress_label, "Progress Bar:");
    lv_obj_set_style_text_font(progress_label, normal_font, 0);
    lv_obj_set_style_text_color(progress_label, lv_color_hex(0xffffff), 0);

    lv_obj_t* progress_bar = lv_bar_create(progress_container);
    lv_obj_set_size(progress_bar, lv_pct(80), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x00ff00), LV_PART_INDICATOR);
    lv_bar_set_value(progress_bar, 70, LV_ANIM_OFF);

    // Slider section
    lv_obj_t* slider_container = lv_obj_create(container);
    lv_obj_set_size(slider_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(slider_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_container, 0, 0);
    lv_obj_set_style_pad_all(slider_container, 10, 0);

    lv_obj_t* slider_label = lv_label_create(slider_container);
    lv_label_set_text(slider_label, "Slider Control:");
    lv_obj_set_style_text_font(slider_label, normal_font, 0);
    lv_obj_set_style_text_color(slider_label, lv_color_hex(0xffffff), 0);

    lv_obj_t* slider = lv_slider_create(slider_container);
    lv_obj_set_width(slider, lv_pct(80));
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);

    // Checkbox section
    lv_obj_t* checkbox_container = lv_obj_create(container);
    lv_obj_set_size(checkbox_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(checkbox_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(checkbox_container, 0, 0);
    lv_obj_set_style_pad_all(checkbox_container, 10, 0);

    lv_obj_t* checkbox_label = lv_label_create(checkbox_container);
    lv_label_set_text(checkbox_label, "Options:");
    lv_obj_set_style_text_font(checkbox_label, normal_font, 0);
    lv_obj_set_style_text_color(checkbox_label, lv_color_hex(0xffffff), 0);

    // Create checkboxes in a row
    lv_obj_t* checkbox_row = lv_obj_create(checkbox_container);
    lv_obj_set_size(checkbox_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(checkbox_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(checkbox_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(checkbox_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(checkbox_row, 0, 0);
    lv_obj_set_style_pad_all(checkbox_row, 5, 0);

    const char* checkbox_texts[] = { "Option 1", "Option 2", "Option 3" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* cb = lv_checkbox_create(checkbox_row);
        lv_checkbox_set_text(cb, checkbox_texts[i]);
        lv_obj_set_style_text_font(cb, normal_font, 0);
        if (i == 1)
            lv_obj_add_state(cb, LV_STATE_CHECKED); // Check the middle one by default
    }

    // LED indicators section (if space allows)
    if (screen_height > 400) {
        lv_obj_t* led_container = lv_obj_create(container);
        lv_obj_set_size(led_container, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(led_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(led_container, 0, 0);
        lv_obj_set_style_pad_all(led_container, 10, 0);

        lv_obj_t* led_label = lv_label_create(led_container);
        lv_label_set_text(led_label, "Status LEDs:");
        lv_obj_set_style_text_font(led_label, normal_font, 0);
        lv_obj_set_style_text_color(led_label, lv_color_hex(0xffffff), 0);

        lv_obj_t* led_row = lv_obj_create(led_container);
        lv_obj_set_size(led_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(led_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(led_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(led_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(led_row, 0, 0);
        lv_obj_set_style_pad_all(led_row, 5, 0);

        // Create LED indicators
        lv_color_t led_colors[] = { lv_color_hex(0xff0000), lv_color_hex(0x00ff00), lv_color_hex(0x0000ff) };
        for (int i = 0; i < 3; i++) {
            lv_obj_t* led      = lv_led_create(led_row);
            int32_t   led_size = screen_width < 480 ? 15 : (screen_width < 800 ? 20 : 25);
            lv_obj_set_size(led, led_size, led_size);
            lv_led_set_color(led, led_colors[i]);
            lv_led_on(led); // Turn on all LEDs
        }
    }

    // Status bar at bottom
    lv_obj_t* status_bar = lv_obj_create(container);
    lv_obj_set_size(status_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 8, 0);

    lv_obj_t* status_label = lv_label_create(status_bar);
    lv_label_set_text(status_label, "Status: Running | Touch: Enabled | FPS: 60");
    lv_obj_set_style_text_font(status_label, normal_font, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_center(status_label);
}

k_s32 sample_connector_init(k_connector_type type)
{
    k_u32            ret = 0;
    k_s32            connector_fd;
    k_u32            chip_id        = 0x00;
    k_connector_type connector_type = type;
    k_connector_info connector_info;

    memset(&connector_info, 0, sizeof(k_connector_info));

    // connector get sensor info
    ret = kd_mpi_get_connector_info(connector_type, &connector_info);
    if (ret) {
        printf("sample_vicap, the sensor type not supported!\n");
        return ret;
    }

    connector_fd = kd_mpi_connector_open(connector_info.connector_name);
    if (connector_fd < 0) {
        printf("%s, connector open failed.\n", __func__);
        return K_ERR_VO_NOTREADY;
    }

    // set connect power
    kd_mpi_connector_power_set(connector_fd, 1);
    // set connect get id
    kd_mpi_connector_id_get(connector_fd, &chip_id);
    // connector init
    kd_mpi_connector_init(connector_fd, connector_info);

    return 0;
}

int vb_init(void)
{
    k_s32                  ret;
    k_vb_config            config;
    k_vb_supplement_config supplement_config;

    memset(&config, 0x00, sizeof(config));
    config.max_pool_cnt = VB_MAX_POOLS;

    // for gdma rotate
    config.comm_pool[0].blk_cnt  = 1;
    config.comm_pool[0].mode     = VB_REMAP_MODE_NOCACHE;
    config.comm_pool[0].blk_size = VB_ALIGN_UP(MAX_DISPLATY_WIDTH * MAX_DISPLATY_HEIGHT * 4, 4096);

    ret = kd_mpi_vb_set_config(&config);
    if (ret) {
        printf("vb_set_config failed ret:%d\n", ret);
        return ret;
    }

    memset(&supplement_config, 0, sizeof(supplement_config));
    supplement_config.supplement_config |= VB_SUPPLEMENT_JPEG_MASK;
    ret = kd_mpi_vb_set_supplement_config(&supplement_config);
    if (ret) {
        printf("vb_set_supplement_config failed ret:%d\n", ret);
        return ret;
    }

    ret = kd_mpi_vb_init();
    if (ret) {
        printf("vb_init failed ret:%d\n", ret);
        return ret;
    }

    return 0;
}

// Print usage information
static void print_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("LVGL K230 OSD Test Application\n\n");
    printf("Options:\n");
    printf("  -c, --connector TYPE    Connector type\n");
    printf("  -r, --rotate DEGREE     Rotation degree: 0, 90, 180, 270 (default: 0)\n");
    printf("  -l, --layer LAYER       OSD layer: 0-3 (default: 3)\n");
    printf("  -f, --format FORMAT     Color format: rgb565, rgb888, argb8888 (default: rgb888)\n");
    printf("  -h, --help              Show this help message\n");
    printf("\n");
}

// Parse rotation degree from argument
static lv_display_rotation_t parse_rotation_degree(int degree)
{
    switch (degree) {
    case 0:
        return LV_DISPLAY_ROTATION_0;
    case 90:
        return LV_DISPLAY_ROTATION_90;
    case 180:
        return LV_DISPLAY_ROTATION_180;
    case 270:
        return LV_DISPLAY_ROTATION_270;
    default:
        return LV_DISPLAY_ROTATION_0;
    }
}

// Parse OSD layer from argument
static k_vo_osd parse_osd_layer(int layer)
{
    switch (layer) {
    case 0:
        return K_VO_OSD0;
    case 1:
        return K_VO_OSD1;
    case 2:
        return K_VO_OSD2;
    case 3:
        return K_VO_OSD3;
    default:
        return K_VO_OSD3;
    }
}

// Parse color format from argument
static lv_color_format_t parse_color_format(const char* format)
{
    if (strcmp(format, "rgb565") == 0) {
        return LV_COLOR_FORMAT_RGB565;
    } else if (strcmp(format, "rgb888") == 0) {
        return LV_COLOR_FORMAT_RGB888;
    } else if (strcmp(format, "argb8888") == 0) {
        return LV_COLOR_FORMAT_ARGB8888;
    } else {
        printf("Unknown color format: %s, using default: rgb888\n", format);
        return LV_COLOR_FORMAT_RGB888;
    }
}

// Get rotation degree as string
static const char* rotation_to_string(lv_display_rotation_t rotation)
{
    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        return "0";
    case LV_DISPLAY_ROTATION_90:
        return "90";
    case LV_DISPLAY_ROTATION_180:
        return "180";
    case LV_DISPLAY_ROTATION_270:
        return "270";
    default:
        return "Unknown";
    }
}

// Get color format as string
static const char* color_format_to_string(lv_color_format_t format)
{
    switch (format) {
    case LV_COLOR_FORMAT_RGB565:
        return "RGB565";
    case LV_COLOR_FORMAT_RGB888:
        return "RGB888";
    case LV_COLOR_FORMAT_ARGB8888:
        return "ARGB8888";
    default:
        return "Unknown";
    }
}

int main(int argc, char* argv[])
{
    lv_display_t*         disp;
    k_connector_type      connector_type = ST7701_V1_MIPI_2LAN_480X800_30FPS;
    k_vo_osd              osd_layer      = K_VO_OSD3;
    lv_display_rotation_t rotation       = LV_DISPLAY_ROTATION_0;
    lv_color_format_t     color_format   = LV_COLOR_FORMAT_RGB888;

    // Default values
    int   rotate_arg = 0; // 0 degrees
    int   layer_arg  = 3; // OSD layer 3
    char* format_arg = "rgb888"; // Default color format

    // Parse command line arguments
    static struct option long_options[] = { { "connector", required_argument, 0, 'c' },
                                            { "rotate", required_argument, 0, 'r' },
                                            { "layer", required_argument, 0, 'l' },
                                            { "format", required_argument, 0, 'f' },
                                            { "help", no_argument, 0, 'h' },
                                            { 0, 0, 0, 0 } };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "c:r:l:f:h", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'c':
            connector_type = atoi(optarg);
            break;
        case 'r':
            rotate_arg = atoi(optarg);
            break;
        case 'l':
            layer_arg = atoi(optarg);
            break;
        case 'f':
            format_arg = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    // Parse the arguments
    rotation     = parse_rotation_degree(rotate_arg);
    osd_layer    = parse_osd_layer(layer_arg);
    color_format = parse_color_format(format_arg);

    // Print configuration
    printf("LVGL K230 OSD Test Starting...\n");
    printf("Configuration:\n");
    printf("  Connector: (%d)\n", connector_type);
    printf("  Rotation: %s degrees\n", rotation_to_string(rotation));
    printf("  OSD Layer: %d\n", layer_arg);
    printf("  Color Format: %s\n", color_format_to_string(color_format));
    printf("\n");

    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);

    // Initialize connector
    if (sample_connector_init(connector_type) != 0) {
        printf("Failed to initialize connector\n");
        return -1;
    }

    vb_init();

    // Initialize LVGL
    lv_init();

    // Create LVGL display on OSD layer
    disp = lv_k230_display_create(connector_type, osd_layer);
    if (!disp) {
        printf("Failed to create LVGL display\n");
        return -1;
    }

    g_display = disp; // Store for signal handler

    printf("LVGL display created successfully\n");

    // Set rotation if specified
    if (rotation != LV_DISPLAY_ROTATION_0) {
        printf("Setting rotation to %s degrees...\n", rotation_to_string(rotation));
        lv_display_set_rotation(disp, rotation);
        printf("Rotation set successfully\n");

        // Print the actual resolution after rotation
        printf("Resolution after rotation: %dx%d\n", lv_display_get_horizontal_resolution(disp),
               lv_display_get_vertical_resolution(disp));
    }

    // Set color format
    printf("Setting color format to %s...\n", color_format_to_string(color_format));
    lv_display_set_color_format(disp, color_format);
    printf("Color format set successfully\n");

    // Create demo widgets
    create_demo_widgets(lv_scr_act());
    printf("Demo widgets created\n");
    printf("Press Ctrl+C to exit...\n");

    // Main loop
    while (!g_signal_received) {
        // Handle LVGL tasks
        uint32_t delay_ms = lv_timer_handler();

        if (100 < delay_ms) {
            delay_ms = 100;
        }

        // Use a shorter sleep to be more responsive to signals
        usleep(delay_ms * 1000);
    }

    // Cleanup
    printf("Cleaning up...\n");
    if (disp) {
        lv_display_delete(disp);
        g_display = NULL;
    }

    printf("LVGL application exited gracefully\n");

    return 0;
}
