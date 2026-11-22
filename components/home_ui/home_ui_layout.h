#pragma once

/* Shared by rendering and hit testing. Rows never grow with external text. */
enum {
    UI_LEFT=24, UI_RIGHT=776, UI_HEADER_LINE=72, UI_STATUS_Y=80,
    UI_ROW_TOP=112, UI_ROW_PITCH=72, UI_TARGET=56, UI_FOOTER=400,
    UI_NAV_Y=416, UI_ACTION_X=536, UI_MORE_X=696,
    UI_MINUS_X=168, UI_PLUS_X=536, UI_VALUE_Y=165, UI_STEP_Y=168,
    UI_PRESET_X=88, UI_PRESET_Y=304, UI_PRESET_PITCH=160
};
static inline int home_ui_row_y(unsigned row) { return UI_ROW_TOP+UI_ROW_PITCH*(int)row; }

enum { NAV_HOME=1,NAV_ROOMS,NAV_SCENES,ART,SETTINGS,BACK,
       OPEN_DEVICE=10,TOGGLE_DEVICE=20,SCENE=30,ROOM=40,MINUS=50,PLUS,APPLY,
       DEMO_PRESET=60,
       LIVE_OPEN=100,LIVE_TOGGLE=140,LIVE_ROOM=180,LIVE_SCENE=200,
       LIVE_PREV=230,LIVE_NEXT,LIVE_LOGIN,LIVE_CANCEL,LIVE_LOGOUT,LIVE_REFRESH,
       LIVE_THEME,LIVE_ACCOUNT,LIVE_NETWORK,
       LIVE_ON=240,LIVE_OFF,LIVE_PRESET=242 };

static inline bool home_ui_device_failed(const home_ui_t *u)
{
    return u->live&&u->live->command_failed&&u->live->failed_device==u->device;
}
