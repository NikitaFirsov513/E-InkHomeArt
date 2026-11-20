#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOME_UI_WIDTH 800U
#define HOME_UI_HEIGHT 480U
#define HOME_UI_BYTES 48000U
#define HOME_UI_IDLE_US INT64_C(45000000)

typedef enum { HOME_PAGE_HOME, HOME_PAGE_ROOMS, HOME_PAGE_ROOM,
               HOME_PAGE_SCENES, HOME_PAGE_DEVICE, HOME_PAGE_SETTINGS } home_ui_page_t;
typedef struct {
    bool active, visible, contact, require_release, pressed;
    uint8_t pointer_id, room, device, return_page, last_scene;
    bool light_on[2], heat_on;
    uint8_t brightness[2], temperature, curtains, draft;
    home_ui_page_t page;
    uint32_t revision, shown_revision;
    unsigned pressed_action;
    int64_t last_activity_us;
} home_ui_t;
typedef struct { uint16_t x,y,width,height; } home_ui_region_t;
typedef struct { unsigned action; int x,y,w,h; const char *label; bool filled; } home_ui_control_t;

void home_ui_init(home_ui_t *ui);
/* Only fresh GT911 frames. Multi-touch / drag / out-of-bounds cancels a click.
 * One wake gesture opens HOME and is swallowed until all fingers lift.
 * Actions execute on release only against the last fully presented revision. */
bool home_ui_touch(home_ui_t *ui,unsigned count,int x,int y,unsigned pointer_id,int64_t now_us);
void home_ui_cancel_touch(home_ui_t *ui);
bool home_ui_tick(home_ui_t *ui,int64_t now_us);
void home_ui_show_art(home_ui_t *ui);
void home_ui_open_menu(home_ui_t *ui,int64_t now_us);
void home_ui_presented(home_ui_t *ui,bool menu,uint32_t revision,int64_t now_us);
bool home_ui_map_touch(uint16_t x,uint16_t y,uint16_t native_w,uint16_t native_h,
                       unsigned orientation,int *screen_x,int *screen_y);
unsigned home_ui_controls(const home_ui_t *ui,home_ui_control_t *out,unsigned capacity);
unsigned home_ui_target(const home_ui_t *ui,int x,int y);
/* 1 bpp, black=1, same native byte polarity as the existing UI bench. */
bool home_ui_render(uint8_t *frame,size_t length,const home_ui_t *ui);
bool home_ui_render_empty_art(uint8_t *frame,size_t length);
bool home_ui_changed_region(const uint8_t *before,const uint8_t *after,size_t length,home_ui_region_t *region);
