#include "diagnostic_screen.h"
#include "touch_test.h"
#include <string.h>

/* Small project-local 5x7 bitmap font. Uppercase ASCII keeps the test firmware
 * independent of GUI libraries, font downloads and filesystem assets. */
static const struct { char code; uint8_t rows[7]; } font[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}},     {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}},   {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}},    {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}},    {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}},    {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'.',{0,0,0,0,0,12,12}},     {',',{0,0,0,0,4,4,8}},
    {':',{0,12,12,0,12,12,0}},   {'-',{0,0,0,31,0,0,0}},
    {'+',{0,4,4,31,4,4,0}},      {'/',{1,2,2,4,8,8,16}},
    {'=',{0,0,31,0,31,0,0}},     {'_',{0,0,0,0,0,0,31}},
    {'?',{14,17,1,2,4,0,4}},     {'!',{4,4,4,4,4,0,4}},
    {'(',{2,4,8,8,8,4,2}},      {')',{8,4,2,2,2,4,8}},
    {'%',{25,25,2,4,8,19,19}},  {'|',{4,4,4,4,4,4,4}},
    {'[',{14,8,8,8,8,8,14}},     {']',{14,2,2,2,2,2,14}},
    {'<',{1,2,4,8,4,2,1}},      {'>',{16,8,4,2,4,8,16}},
};

static void rect(uint8_t *f, int x, int y, int w, int h, bool black)
{
    if (w <= 0 || h <= 0) return;
    int64_t right = (int64_t)x+w, bottom = (int64_t)y+h;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = right > DIAG_WIDTH ? DIAG_WIDTH : (int)right;
    int y1 = bottom > DIAG_HEIGHT ? DIAG_HEIGHT : (int)bottom;
    if (x0 >= x1 || y0 >= y1) return;
    unsigned first = (unsigned)x0/8, last = (unsigned)(x1-1)/8;
    uint8_t left_mask = (uint8_t)(0xffU >> (x0&7));
    uint8_t right_mask = (uint8_t)(0xffU << (7-((x1-1)&7)));
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t *row = f+(size_t)yy*(DIAG_WIDTH/8);
        uint8_t mask = first == last ? left_mask & right_mask : left_mask;
        if (black) row[first] |= mask;
        else row[first] &= (uint8_t)~mask;
        if (first == last) continue;
        if (last > first+1) memset(row+first+1,black ? 0xff : 0,last-first-1);
        if (black) row[last] |= right_mask;
        else row[last] &= (uint8_t)~right_mask;
    }
}

static void box(uint8_t *f, int x, int y, int w, int h)
{
    rect(f,x,y,w,1,true); rect(f,x,y+h-1,w,1,true);
    rect(f,x,y,1,h,true); rect(f,x+w-1,y,1,h,true);
}

static void text(uint8_t *f, int x, int y, const char *s, size_t limit, int scale, bool black)
{
    for (size_t n = 0; n < limit && s[n] && x + 5 * scale <= DIAG_WIDTH; ++n, x += 6 * scale) {
        char ch = s[n];
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        const uint8_t *glyph = NULL;
        for (size_t g = 0; g < sizeof(font)/sizeof(font[0]); ++g)
            if (font[g].code == ch) { glyph = font[g].rows; break; }
        if (!glyph) continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyph[row] & (16 >> col)) rect(f,x+col*scale,y+row*scale,scale,scale,black);
    }
}

static const int targets[5][2] = {{40,110},{760,110},{40,390},{760,390},{400,250}};

static void cross(uint8_t *f, int x, int y, int radius)
{
    rect(f,x-radius,y,2*radius+1,1,true);
    rect(f,x,y-radius,1,2*radius+1,true);
    box(f,x-radius,y-radius,2*radius+1,2*radius+1);
}

static void header(uint8_t *f, const char *subtitle)
{
    rect(f,0,0,800,64,true);
    text(f,22,17,"E-INK HOME ART",32,4,false);
    text(f,550,17,subtitle,20,2,false);
    text(f,550,39,"BOARD TEST / V1",20,1,false);
}

static void footer(uint8_t *f, bool display_touch)
{
    rect(f,0,430,800,50,false);
    rect(f,20,430,760,2,true);
    text(f,22,440,display_touch ? "BOOT: SNAPSHOT | HOLD BOOT: INVERT | UART: N NEXT" :
         "POWER: NEXT | BOOT: SNAPSHOT | HOLD BOOT: INVERT",64,2,true);
    text(f,22,463,display_touch ? "UART + ENTER: R REFRESH  N NEXT  I INVERT  T TOUCH MAP  C CLEAR  ? HELP" :
         "USB: ? HELP  R REFRESH  N NEXT  I INVERT  T TOUCH MAP  C CLEAR",78,1,true);
}

uint32_t diagnostic_target_hit(int x, int y)
{
    if (x < 0 || y < 0 || x >= DIAG_WIDTH || y >= DIAG_HEIGHT) return 0;
    uint32_t mask = 0;
    for (unsigned i = 0; i < 5; ++i) {
        int dx = x - targets[i][0], dy = y - targets[i][1];
        if (dx >= -30 && dx <= 30 && dy >= -30 && dy <= 30) mask |= 1U << i;
    }
    return mask;
}

bool diagnostic_map_touch(uint16_t raw_x, uint16_t raw_y, uint16_t w,
                          uint16_t h, unsigned orientation, int *x, int *y)
{
    if (!x || !y || w < 2 || h < 2 || raw_x >= w || raw_y >= h || orientation > 7) return false;
    unsigned xx = raw_x, yy = raw_y;
    if ((w < h) != ((orientation & 1U) != 0)) {
        unsigned tmp = xx; xx = yy; yy = tmp;
        uint16_t t = w; w = h; h = t;
    }
    *x = (int)(xx * (DIAG_WIDTH - 1U) / (w - 1U));
    *y = (int)(yy * (DIAG_HEIGHT - 1U) / (h - 1U));
    if (orientation & 2U) *x = DIAG_WIDTH - 1 - *x;
    if (orientation & 4U) *y = DIAG_HEIGHT - 1 - *y;
    return true;
}

static bool render(uint8_t *f, size_t length, const diagnostic_screen_t *s, bool display_touch)
{
    if (!f || !s || length != DIAG_FRAME_BYTES || s->page >= DIAG_PAGE_COUNT) return false;
    for (size_t i = 0; i < length; ++i) f[i] = s->page == DIAG_BLACK ? 0xFF : 0;
    if (s->page == DIAG_STATUS) {
        header(f,display_touch ? "DISPLAY TEST" : "DIAGNOSTICS");
        text(f,22,82,"HARDWARE / STARTUP",32,2,true);
        for (unsigned i = 0; i < DIAG_LINE_COUNT; ++i)
            text(f,22,112+(int)i*27,s->lines[i],DIAG_LINE_LENGTH,2,true);
        box(f,505,82,275,245);
        text(f,523,97,"OPTICAL CHECK",24,2,true);
        for (int band = 0; band < 3; ++band) {
            int step = 1 << band;
            for (int x = 0; x < 70; ++x)
                rect(f,521+band*84+x,129,1,66,(x/step)%2 == 0);
            box(f,521+band*84,129,70,66);
        }
        text(f,532,205,"1PX    2PX    4PX",24,2,true);
        rect(f,523,237,113,55,true); box(f,649,237,113,55);
        text(f,535,257,"BIT=1",12,2,false); text(f,661,257,"BIT=0",12,2,true);
        text(f,523,306,"COMPARE BOTH AREAS",24,1,true);
        rect(f,20,342,760,1,true);
        text(f,22,355,s->details[0],64,2,true);
        text(f,22,382,s->details[1],64,2,true);
        text(f,22,409,display_touch ? "VALUES ARE A SNAPSHOT; LIVE TOUCH DATA IN UART0" :
             "VALUES ARE A SNAPSHOT; LIVE DATA IN USB CONSOLE",64,1,true);
        footer(f,display_touch);
    } else if (s->page == DIAG_GRID) {
        for (int x = 0; x < DIAG_WIDTH; x += 25) rect(f,x,65,1,365,true);
        for (int y = 80; y < 430; y += 25) rect(f,0,y,800,1,true);
        header(f,"TOUCH / GRID");
        for (unsigned i = 0; i < 5; ++i) {
            cross(f,targets[i][0],targets[i][1],15);
            if (s->target_mask & (1U << i)) rect(f,targets[i][0]-6,targets[i][1]-6,13,13,true);
        }
        if (s->touch_samples && s->touch_x >= 0 && s->touch_y >= 0 &&
            s->touch_x < DIAG_WIDTH && s->touch_y < DIAG_HEIGHT)
            cross(f,s->touch_x,s->touch_y,9);
        rect(f,145,96,510,40,false);
        text(f,157,107,"TOUCH 5 TARGETS THEN PRESS BOOT",48,2,true);
        rect(f,145,341,510,65,false);
        text(f,157,351,s->details[0],40,2,true);
        text(f,157,378,s->details[1],40,2,true);
        footer(f,display_touch);
    } else if (s->page == DIAG_CHECKER) {
        for (int y = 0; y < DIAG_HEIGHT; ++y)
            for (int bx = 0; bx < DIAG_WIDTH/8; ++bx)
                f[y*(DIAG_WIDTH/8)+bx] = ((bx/2+y/16)%2 == 0) ? 0xff : 0;
        rect(f,240,206,320,68,false); box(f,240,206,320,68);
        text(f,258,222,"16PX CHECKER",24,4,true);
    }
    if (s->inverted) for (size_t i = 0; i < length; ++i) f[i] ^= 0xFF;
    return true;
}

bool diagnostic_screen_render(uint8_t *f, size_t length, const diagnostic_screen_t *s)
{
    return render(f,length,s,false);
}

bool display_touch_screen_render(uint8_t *f, size_t length, const diagnostic_screen_t *s)
{
    return render(f,length,s,true);
}

static void trace_line(uint8_t *f, int x, int y, int end_x, int end_y)
{
    int dx = end_x-x, dy = end_y-y;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy > 0) dy = -dy;
    int error = dx+dy;
    for (;;) {
        if (y >= 80 && y < 416) rect(f,x,y,2,2,true);
        if (x == end_x && y == end_y) break;
        int twice = 2*error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

bool touch_test_screen_render(uint8_t *f, size_t length, const touch_test_t *t)
{
    if (!t || t->trace_count > TOUCH_TEST_TRACE || t->trace_next >= TOUCH_TEST_TRACE ||
        t->contacts > TOUCH_TEST_POINTS) return false;
    for (unsigned i = 0; i < t->trace_count; ++i)
        if (t->trace[i].x >= DIAG_WIDTH || t->trace[i].y >= DIAG_HEIGHT) return false;
    diagnostic_screen_t s = t->screen;
    s.inverted = 0;
    if (!render(f,length,&s,true)) return false;
    if (s.page == DIAG_GRID) {
        for (unsigned i = 0; i < TOUCH_TEST_CELLS; ++i) {
            int x = (int)(i%8)*100, y = 80+(int)(i/8)*112;
            box(f,x,y,100,112);
            if (t->coverage & (UINT32_C(1)<<i)) rect(f,x+4,y+4,7,7,true);
        }
        unsigned start = (t->trace_next+TOUCH_TEST_TRACE-t->trace_count)%TOUCH_TEST_TRACE;
        for (unsigned i = 0; i < t->trace_count; ++i) {
            const touch_test_trace_t *p = &t->trace[(start+i)%TOUCH_TEST_TRACE];
            const touch_test_trace_t *prev = &t->trace[(start+i+TOUCH_TEST_TRACE-1)%TOUCH_TEST_TRACE];
            if (i && prev->stroke == p->stroke) trace_line(f,prev->x,prev->y,p->x,p->y);
            else if (p->y >= 80 && p->y < 416) rect(f,p->x,p->y,3,3,true);
        }
        for (unsigned i = 0; i < t->contacts; ++i) {
            const touch_test_point_t *p = &t->points[i];
            if (p->x < DIAG_WIDTH && p->y >= 80 && p->y < 416) cross(f,p->x,p->y,11);
        }
        rect(f,145,96,510,40,false);
        text(f,157,107,"5 TARGETS / TRACE / 24 CELLS",40,2,true);
        rect(f,145,341,510,65,false);
        text(f,157,351,s.details[0],40,2,true);
        text(f,157,378,s.details[1],40,2,true);
    }
    if (s.page == DIAG_GRID || s.page == DIAG_STATUS) {
        rect(f,550,39,240,12,true);
        text(f,550,39,"TOUCH BENCH / V6",20,1,false);
        rect(f,0,430,800,50,false);
        const char *labels[] = {"SNAPSHOT", "CLEAR", "NEXT PAGE"};
        for (unsigned i = 0; i < 3; ++i) {
            int x = 20+(int)i*260;
            box(f,x,438,240,40);
            text(f,x+20,450,labels[i],16,2,true);
        }
    }
    if (t->screen.inverted) for (size_t i = 0; i < length; ++i) f[i] ^= 0xff;
    return true;
}
