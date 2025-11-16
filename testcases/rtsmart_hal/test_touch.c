#include "drv_touch.h"
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Global flag for graceful shutdown */
static volatile bool g_running = true;

/* Signal handler for Ctrl-C */
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        printf("\nReceived SIGINT (Ctrl-C), shutting down gracefully...\n");
        g_running = false;
    }
}

/* Setup signal handling */
void setup_signal_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

/* Print touch event as string */
const char* touch_event_to_string(uint8_t event)
{
    switch (event) {
    case DRV_TOUCH_EVENT_NONE:
        return "NONE";
    case DRV_TOUCH_EVENT_UP:
        return "UP";
    case DRV_TOUCH_EVENT_DOWN:
        return "DOWN";
    case DRV_TOUCH_EVENT_MOVE:
        return "MOVE";
    default:
        return "UNKNOWN";
    }
}

/* Dump touch data in readable format */
void dump_touch_data(const struct drv_touch_data* touch_data, int point_count, int read_cycle)
{
    printf("\n=== Touch Data Read Cycle #%d ===\n", read_cycle);
    printf("Points detected: %d\n", point_count);
    printf("----------------------------------------\n");

    if (point_count <= 0) {
        printf("No touch points detected\n");
        return;
    }

    for (int i = 0; i < point_count; i++) {
        const struct drv_touch_data* point = &touch_data[i];
        printf("Point %d:\n", i);
        printf("  Event:      %s (%d)\n", touch_event_to_string(point->event), point->event);
        printf("  Track ID:   %d\n", point->track_id);
        printf("  Position:   X=%d, Y=%d\n", point->x_coordinate, point->y_coordinate);
        printf("  Width:      %d\n", point->width);
        printf("  Timestamp:  %u ms\n", point->timestamp);
        printf("----------------------------------------\n");
    }
}

/* Print device information */
void print_device_info(drv_touch_inst_t* inst)
{
    struct drv_touch_info info;

    if (drv_touch_get_info(inst, &info) == 0) {
        printf("=== Touch Device Information ===\n");
        printf("Type:      %d\n", info.type);
        printf("Vendor:    %d\n", info.vendor);
        printf("Max Points: %d\n", info.point_num);
        printf("X Range:   0-%u\n", info.range_x);
        printf("Y Range:   0-%u\n", info.range_y);
        printf("===============================\n\n");
    } else {
        printf("Failed to get device information\n");
    }
}

/* Get rotation information */
void print_rotation_info(drv_touch_inst_t* inst)
{
    int rotate;

    if (drv_touch_get_default_rotate(inst, &rotate) == 0) {
        const char* rotation_str;
        switch (rotate) {
        case DRV_TOUCH_ROTATE_DEGREE_0:
            rotation_str = "0 degrees";
            break;
        case DRV_TOUCH_ROTATE_DEGREE_90:
            rotation_str = "90 degrees";
            break;
        case DRV_TOUCH_ROTATE_DEGREE_180:
            rotation_str = "180 degrees";
            break;
        case DRV_TOUCH_ROTATE_DEGREE_270:
            rotation_str = "270 degrees";
            break;
        case DRV_TOUCH_ROTATE_SWAP_XY:
            rotation_str = "Swap XY";
            break;
        default:
            rotation_str = "Unknown";
            break;
        }
        printf("Default Rotation: %s (%d)\n\n", rotation_str, rotate);
    } else {
        printf("Failed to get rotation information\n\n");
    }
}

int main(void)
{
    drv_touch_inst_t*     touch_inst = NULL;
    struct drv_touch_data touch_data[5]; // Buffer for max 5 points
    int                   read_cycle = 0;
    int                   ret;

    printf("Touch Device Test Application\n");
    printf("=============================\n");
    printf("This application will:\n");
    printf("1. Open touch device 0\n");
    printf("2. Display device information\n");
    printf("3. Continuously read touch data (max 5 points)\n");
    printf("4. Dump touch data in readable format\n");
    printf("5. Handle Ctrl-C for graceful shutdown\n\n");

    /* Setup signal handler for Ctrl-C */
    setup_signal_handler();

    /* Create touch instance for device 0 */
    printf("Opening touch device 0...\n");
    ret = drv_touch_inst_create(0, &touch_inst);
    if (ret != 0 || touch_inst == NULL) {
        fprintf(stderr, "Failed to create touch instance for device 0 (error: %d)\n", ret);
        return EXIT_FAILURE;
    }
    printf("Successfully opened touch device 0\n\n");

    /* Display device information */
    print_device_info(touch_inst);
    print_rotation_info(touch_inst);

    printf("Starting touch data reading loop...\n");
    printf("Press Ctrl-C to stop the application\n\n");

    /* Main reading loop */
    while (g_running) {
        /* Read touch data with max 5 points */
        int point_count = drv_touch_read(touch_inst, touch_data, 5);

        if (point_count > 0) {
            /* Successfully read touch data */
            dump_touch_data(touch_data, point_count, ++read_cycle);
        } else if (point_count == 0) {
            /* No data available (non-blocking read) */
            printf(".");
        } else {
            /* Error occurred */
            if (point_count == -1) {
                fprintf(stderr, "Error: Invalid parameters in drv_touch_read\n");
            } else if (point_count == -2) {
                /* This is normal for non-blocking read when no data is available */
                printf(".");
            } else {
                fprintf(stderr, "Error: Unknown error in drv_touch_read (%d)\n", point_count);
            }
        }

        /* Small delay to prevent excessive CPU usage */
        usleep(10000); // 10ms delay
    }

    /* Cleanup */
    printf("\nCleaning up resources...\n");
    if (touch_inst != NULL) {
        drv_touch_inst_destroy(&touch_inst);
        printf("Touch instance destroyed\n");
    }

    printf("Application terminated gracefully\n");
    return EXIT_SUCCESS;
}
