#pragma once
#include <stdbool.h>
#include <stdint.h>
#define HOME_MAX_DEVICES 32
#define HOME_MAX_ROOMS 16
#define HOME_MAX_SCENES 16
#define HOME_MAX_CONTROLS 384
#define HOME_MAX_OPTIONS 1024

typedef enum { HOME_CONTROL_TOGGLE,HOME_CONTROL_RANGE,HOME_CONTROL_MODE,
               HOME_CONTROL_RGB,HOME_CONTROL_HSV,HOME_CONTROL_READING,
               HOME_CONTROL_UNSUPPORTED } home_control_kind_t;
typedef enum {
    HOME_PROFILE_GENERIC,HOME_PROFILE_LIGHT,HOME_PROFILE_CLIMATE,HOME_PROFILE_HUMIDIFIER,
    HOME_PROFILE_AIR,HOME_PROFILE_FAN,HOME_PROFILE_MEDIA,HOME_PROFILE_VACUUM,
    HOME_PROFILE_OPENABLE,HOME_PROFILE_LOCK,HOME_PROFILE_POWER,HOME_PROFILE_KETTLE,
    HOME_PROFILE_COFFEE,HOME_PROFILE_COOKER,HOME_PROFILE_DISHWASHER,HOME_PROFILE_WASHER,
    HOME_PROFILE_IRON,HOME_PROFILE_PET_FEEDER,HOME_PROFILE_PET_WATER,
    HOME_PROFILE_SENSOR,HOME_PROFILE_METER,HOME_PROFILE_CAMERA
} home_device_profile_t;
typedef struct {
    char name[64],state[80],unit[16];
    char instance[48],on_label[64],off_label[64];
    home_control_kind_t kind;
    uint16_t device,source,option_start,option_count,selected_option,priority;
    bool known,boolean,relative_only,has_minimum,has_maximum,momentary;
    bool quick;
    double value,second,third,minimum,maximum,step;
} home_live_control_t;
typedef struct { char name[64]; } home_live_option_t;
typedef enum { HOME_ICON_POWER,HOME_ICON_LIGHT,HOME_ICON_HEAT,HOME_ICON_CURTAIN } home_device_icon_t;
typedef struct {
    char name[80],state[80],range_name[40],unit[12];
    char type[80],quick_on[32],quick_off[32];
    home_device_profile_t profile;
    home_device_icon_t icon;
    uint8_t room;
    bool available,has_toggle,on,has_range;
    double value,minimum,maximum,step;
    uint16_t control_start,control_count;
    uint16_t primary_control;
    bool controls_truncated,dashboard;
} home_live_device_t;
typedef struct { char name[80]; } home_live_name_t;
typedef struct {
    uint32_t epoch;
    bool online,time_ready,authorized,waiting,pending,loaded,truncated;
    /* Explicit result state: UI must offer a GET, never retry an uncertain POST. */
    bool command_failed;
    uint8_t failed_device; /* HOME_MAX_DEVICES for a scene/non-device command. */
    char ip[16],code[40],url[256],message[256];
    uint8_t device_count,room_count,scene_count;
    home_live_device_t devices[HOME_MAX_DEVICES];
    home_live_name_t rooms[HOME_MAX_ROOMS],scenes[HOME_MAX_SCENES];
    uint16_t control_count,option_count;
    home_live_control_t controls[HOME_MAX_CONTROLS];
    home_live_option_t options[HOME_MAX_OPTIONS];
} home_live_data_t;
typedef enum { HOME_CMD_NONE,HOME_CMD_LOGIN,HOME_CMD_CANCEL,HOME_CMD_LOGOUT,
               HOME_CMD_REFRESH,HOME_CMD_TOGGLE,HOME_CMD_RANGE,HOME_CMD_SCENE,HOME_CMD_CONTROL } home_command_kind_t;
typedef struct {
    home_command_kind_t kind; uint32_t epoch; unsigned slot; double value;
    unsigned control,option; double second,third; bool relative;
} home_command_t;
