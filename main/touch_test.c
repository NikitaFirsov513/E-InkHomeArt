#include "touch_test.h"

touch_refresh_t touch_test_refresh_policy(const touch_test_t *test, bool baseline,
    unsigned partials, unsigned limit, uint32_t completed_generation, int64_t now_us)
{
    if (!baseline || test->full_generation != completed_generation) return TOUCH_REFRESH_FULL;
    if (partials < limit) return TOUCH_REFRESH_PARTIAL;
    if (test->contacts || now_us-test->last_activity_us < TOUCH_TEST_CLEAN_IDLE_US)
        return TOUCH_REFRESH_WAIT;
    return TOUCH_REFRESH_FULL;
}

bool touch_test_changed_region(const uint8_t *before, const uint8_t *after, size_t length,
                               touch_test_region_t *r)
{
    if (!r) return false;
    *r = (touch_test_region_t){0};
    if (!before || !after || length != DIAG_FRAME_BYTES) return false;
    unsigned left = DIAG_WIDTH/8, right = 0, top = DIAG_HEIGHT, bottom = 0;
    for (unsigned y = 0; y < DIAG_HEIGHT; ++y) for (unsigned x = 0; x < DIAG_WIDTH/8; ++x) {
        size_t i = y*(DIAG_WIDTH/8)+x;
        if (before[i] == after[i]) continue;
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
    }
    if (top == DIAG_HEIGHT) return false;
    if (left == right) { if (right+1 < DIAG_WIDTH/8) ++right; else --left; }
    if (top == bottom) { if (bottom+1 < DIAG_HEIGHT) ++bottom; else --top; }
    *r = (touch_test_region_t){left*8,top,(right-left+1)*8,bottom-top+1};
    return true;
}

void touch_test_clear(touch_test_t *t)
{
    if (!t) return;
    t->screen.touch_samples = t->screen.target_mask = 0;
    t->screen.touch_x = t->screen.touch_y = -1;
    t->contacts = t->max_contacts = t->strokes = t->coverage = t->errors = 0;
    t->trace_count = t->trace_next = t->trace_stroke = 0;
    t->trace_active = false;
}

char touch_test_button(int x, int y)
{
    if (y < 438 || y >= DIAG_HEIGHT) return 0;
    if (x >= 20 && x < 260) return 'r';
    if (x >= 280 && x < 520) return 'c';
    if (x >= 540 && x < 780) return 'n';
    return 0;
}

unsigned touch_test_next_ui_page(unsigned shown_page)
{
    return shown_page == DIAG_GRID ? DIAG_STATUS : DIAG_GRID;
}

bool touch_test_controls_ready(unsigned shown_page, unsigned requested_page)
{
    return shown_page == requested_page && (shown_page == DIAG_GRID || shown_page == DIAG_STATUS);
}

bool touch_test_accept(touch_test_t *t, const touch_test_point_t *raw,
                       unsigned count, uint16_t width, uint16_t height)
{
    if (!t || count > TOUCH_TEST_POINTS || (count && !raw)) return false;
    touch_test_point_t mapped[TOUCH_TEST_POINTS];
    for (unsigned i = 0; i < count; ++i) {
        int x, y;
        if (!diagnostic_map_touch(raw[i].x,raw[i].y,width,height,
                                  t->screen.orientation,&x,&y)) return false;
        mapped[i] = raw[i];
        mapped[i].x = (uint16_t)x;
        mapped[i].y = (uint16_t)y;
    }
    if (!count) {
        t->contacts = 0;
        t->trace_active = false;
        return true;
    }
    if (!t->contacts) ++t->strokes;
    t->contacts = count;
    if (count > t->max_contacts) t->max_contacts = count;
    ++t->screen.touch_samples;
    for (unsigned i = 0; i < count; ++i) {
        t->points[i] = mapped[i];
        const int x = mapped[i].x, y = mapped[i].y;
        t->screen.target_mask |= diagnostic_target_hit(x,y);
        if (y >= 80 && y < 416)
            t->coverage |= UINT32_C(1) << ((unsigned)(y-80)/112*8+(unsigned)x/100);
    }
    t->screen.touch_x = mapped[0].x;
    t->screen.touch_y = mapped[0].y;
    /* Only a single-finger stroke becomes a line: never join different fingers,
     * a release, or the first contact after a multitouch frame. */
    if (count != 1) { t->trace_active = false; return true; }
    if (!t->trace_active || t->trace_id != mapped[0].id) {
        ++t->trace_stroke;
        t->trace_active = true;
        t->trace_id = mapped[0].id;
    }
    if (t->trace_count) {
        const touch_test_trace_t *last = &t->trace[(t->trace_next+TOUCH_TEST_TRACE-1)%TOUCH_TEST_TRACE];
        int dx = (int)mapped[0].x-last->x, dy = (int)mapped[0].y-last->y;
        if (last->stroke == t->trace_stroke && dx*dx+dy*dy < 9) return true;
    }
    t->trace[t->trace_next] = (touch_test_trace_t){mapped[0].x,mapped[0].y,t->trace_stroke};
    t->trace_next = (t->trace_next+1)%TOUCH_TEST_TRACE;
    if (t->trace_count < TOUCH_TEST_TRACE) ++t->trace_count;
    return true;
}
