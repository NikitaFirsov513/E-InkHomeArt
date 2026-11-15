#pragma once

#include "diagnostic_screen.h"

#define TOUCH_TEST_POINTS 5
#define TOUCH_TEST_TRACE 128
#define TOUCH_TEST_CELLS 24
#define TOUCH_TEST_ALL_CELLS UINT32_C(0x00ffffff)
#define TOUCH_TEST_CLEAN_IDLE_US INT64_C(1200000)
typedef struct { uint16_t x, y, width, height; } touch_test_region_t;
/* Smallest changed byte rectangle, expanded to at least 16x2 within panel. */
bool touch_test_changed_region(const uint8_t *before, const uint8_t *after, size_t length,
                               touch_test_region_t *region);

typedef struct {
    uint16_t x, y, size, id;
} touch_test_point_t;

typedef struct {
    uint16_t x, y;
    uint32_t stroke;
} touch_test_trace_t;

typedef struct {
    diagnostic_screen_t screen;
    uint32_t contacts, max_contacts, strokes, coverage, errors;
    uint32_t trace_count, trace_next;
    touch_test_point_t points[TOUCH_TEST_POINTS];
    touch_test_trace_t trace[TOUCH_TEST_TRACE];
    uint32_t trace_stroke;
    uint16_t trace_id;
    bool trace_active;
    uint32_t full_generation;
    int64_t last_activity_us;
} touch_test_t;

typedef enum { TOUCH_REFRESH_PARTIAL, TOUCH_REFRESH_FULL, TOUCH_REFRESH_WAIT } touch_refresh_t;
/* Exhausted partial budget waits for release + quiet time. A manual full request
 * survives overwritten snapshots and deliberately overrides this idle gate. */
touch_refresh_t touch_test_refresh_policy(const touch_test_t *test, bool baseline,
    unsigned partials, unsigned limit, uint32_t completed_generation, int64_t now_us);

void touch_test_clear(touch_test_t *test);
/* Call only for a fresh controller report. count=0 is an actual release.
 * Returns false without changing the model for malformed coordinates/count. */
bool touch_test_accept(touch_test_t *test, const touch_test_point_t *raw,
                       unsigned count, uint16_t width, uint16_t height);
/* Fixed on-screen controls, enabled on GRID/STATUS only. */
char touch_test_button(int x, int y);
/* Physical NEXT cycles interactive pages; pure fills remain UART-only. */
unsigned touch_test_next_ui_page(unsigned shown_page);
bool touch_test_controls_ready(unsigned shown_page, unsigned requested_page);
bool touch_test_screen_render(uint8_t *frame, size_t length, const touch_test_t *test);
