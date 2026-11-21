#pragma once
#include <stdbool.h>
#include <stdint.h>
#define HOME_MAX_DEVICES 32
#define HOME_MAX_ROOMS 16
#define HOME_MAX_SCENES 16
typedef struct {
    char name[80],state[80],range_name[40];
    uint8_t room;
    bool available,has_toggle,on,has_range;
    double value,minimum,maximum,step;
} home_live_device_t;
typedef struct { char name[80]; } home_live_name_t;
typedef struct {
    uint32_t epoch;
    bool online,time_ready,authorized,waiting,pending,loaded,truncated;
    char ip[16],code[40],url[256],message[256];
    uint8_t device_count,room_count,scene_count;
    home_live_device_t devices[HOME_MAX_DEVICES];
    home_live_name_t rooms[HOME_MAX_ROOMS],scenes[HOME_MAX_SCENES];
} home_live_data_t;
typedef enum { HOME_CMD_NONE,HOME_CMD_LOGIN,HOME_CMD_CANCEL,HOME_CMD_LOGOUT,
               HOME_CMD_REFRESH,HOME_CMD_TOGGLE,HOME_CMD_RANGE,HOME_CMD_SCENE } home_command_kind_t;
typedef struct { home_command_kind_t kind; uint32_t epoch; unsigned slot; double value; } home_command_t;
