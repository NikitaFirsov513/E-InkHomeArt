#pragma once

/* Shared by rendering and hit testing. External text cannot resize a tile. */
enum {
    UI_LEFT=24, UI_RIGHT=776, UI_HEADER_LINE=72, UI_STATUS_Y=80,
    UI_ROW_TOP=112, UI_ROW_PITCH=72, UI_TARGET=56, UI_FOOTER=400,
    UI_NAV_Y=416, UI_ACTION_X=536, UI_MORE_X=696,
    UI_MINUS_X=168, UI_PLUS_X=536, UI_VALUE_Y=165, UI_STEP_Y=168,
    UI_PRESET_X=88, UI_PRESET_Y=304, UI_PRESET_PITCH=160
};
static inline int home_ui_row_y(unsigned row) { return UI_ROW_TOP+UI_ROW_PITCH*(int)row; }
enum { UI_TILE_WIDTH=240, UI_TILE_HEIGHT=128, UI_TILE_PITCH_X=256,
       UI_TILE_PITCH_Y=144, UI_TILE_PAD=12, UI_TILE_BUTTON_Y=64,
       UI_TILE_MORE_X=172 };
static inline int home_ui_tile_x(unsigned tile) { return UI_LEFT+UI_TILE_PITCH_X*(int)(tile%3); }
static inline int home_ui_tile_y(unsigned tile) { return UI_ROW_TOP+UI_TILE_PITCH_Y*(int)(tile/3); }

enum { NAV_HOME=1,NAV_ROOMS,NAV_SCENES,ART,SETTINGS,BACK,
       OPEN_DEVICE=10,TOGGLE_DEVICE=20,SCENE=30,ROOM=40,MINUS=50,PLUS,APPLY,
       DEMO_PRESET=60,
       LIVE_OPEN=100,LIVE_TOGGLE=140,LIVE_ROOM=180,LIVE_SCENE=200,
       LIVE_PREV=230,LIVE_NEXT,LIVE_LOGIN,LIVE_CANCEL,LIVE_LOGOUT,LIVE_REFRESH,
       LIVE_THEME,LIVE_ACCOUNT,LIVE_NETWORK,
       LIVE_ON=240,LIVE_OFF,LIVE_PRESET=242,
       CAP_PREV=300,CAP_NEXT,CAP_ON,CAP_OFF,CAP_PULSE,CAP_EDIT,CAP_OPTIONS_PREV,CAP_OPTIONS_NEXT,CAP_ALL,
       CAP_COLOR_MINUS=320,CAP_COLOR_PLUS=330,CAP_COLOR_EDIT=340,
       CAP_OPEN=1000,CAP_OPTION=2000,NUMBER_KEY=4000,CAP_QUICK=5000 };

static inline bool home_ui_capabilities(const home_ui_t *u)
{ return u->live&&u->device<u->live->device_count&&u->live->devices[u->device].control_count>0; }

static inline bool home_ui_device_failed(const home_ui_t *u)
{
    return u->live&&u->live->command_failed&&u->live->failed_device==u->device;
}
