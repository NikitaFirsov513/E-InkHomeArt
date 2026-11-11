#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIAG_WIDTH 800
#define DIAG_HEIGHT 480
#define DIAG_FRAME_BYTES (DIAG_WIDTH * DIAG_HEIGHT / 8)
#define DIAG_LINE_COUNT 8
#define DIAG_LINE_LENGTH 40

typedef enum {
    DIAG_STATUS,
    DIAG_GRID,
    DIAG_WHITE,
    DIAG_BLACK,
    DIAG_CHECKER,
    DIAG_PAGE_COUNT,
} diagnostic_page_t;

typedef struct {
    uint32_t page;
    uint32_t inverted;
    uint32_t orientation;
    uint32_t touch_samples;
    uint32_t target_mask;
    int32_t touch_x;
    int32_t touch_y;
    char lines[DIAG_LINE_COUNT][DIAG_LINE_LENGTH];
    char details[2][64];
} diagnostic_screen_t;

/* Pure renderer: 1 means black in the logical frame, MSB first, row-major.
 * Caller may flip all bytes to match native panel polarity. No hardware access. */
bool diagnostic_screen_render(uint8_t *frame, size_t length, const diagnostic_screen_t *screen);
/* Orientation 0 auto-swaps a portrait sensor to landscape; 1 toggles that swap,
 * bit 1 mirrors X and bit 2 mirrors Y. Raw sensor config is never overwritten. */
bool diagnostic_map_touch(uint16_t raw_x, uint16_t raw_y, uint16_t native_width,
                          uint16_t native_height, unsigned orientation, int *x, int *y);
uint32_t diagnostic_target_hit(int x, int y);
