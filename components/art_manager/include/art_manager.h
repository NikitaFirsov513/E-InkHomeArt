#pragma once
#include "art_store.h"
#include "uc8179.h"
#include "home_live.h"

typedef enum {
    ART_DISPLAY_EMPTY, ART_DISPLAY_QUEUED, ART_DISPLAY_BUSY,
    ART_DISPLAY_READY, ART_DISPLAY_ERROR
} art_display_state_t;

typedef struct {
    bool storage_ready, has_frame, uploading;
    uint32_t saved_generation, displayed_generation, pixel_crc;
    art_display_state_t display;
    esp_err_t display_error;
    art_store_result_t store_error;
    bool menu_visible,menu_requested,dark_theme;
    uint32_t ui_revision,ui_shown_revision;
} art_manager_status_t;

/* Singleton owner of the store, one 96064-byte packet buffer, and panel task.
 * Panel is borrowed; NULL allows storage/upload with a reported display error.
 * Allocate before Wi-Fi starts. No NVS/partition erase or format at startup. */
esp_err_t art_manager_start(uc8179_handle_t panel);
void art_manager_status(art_manager_status_t *out);
/* Nonblocking exclusive buffer lease. HTTP task must commit or abort in the
 * SAME task. It may receive chunks into this buffer; no Flash writes until
 * the entire validated packet is committed. Busy display/upload -> 409. */
esp_err_t art_manager_begin_upload(uint8_t **packet);
art_store_result_t art_manager_commit_upload(size_t length);
void art_manager_abort_upload(void);
/* Fresh, mapped touch frames only. Main task remains free while EPD is BUSY. */
void art_manager_touch(unsigned count,int x,int y,unsigned id,int64_t now_us);
void art_manager_cancel_touch(void);
void art_manager_open_menu(void);
void art_manager_show_art(void);

/* Production attaches a live model before networking; backend publishes copies.
 * Callback queues only and runs outside the UI spinlock. */
esp_err_t art_manager_enable_home(bool (*submit)(const home_command_t *));
void art_manager_home_update(const home_live_data_t *data);

void art_manager_set_dark_theme(bool dark);
