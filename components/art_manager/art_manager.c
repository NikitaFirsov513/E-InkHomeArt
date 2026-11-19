#include "art_manager.h"
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "art_store_esp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define REFRESH_GAP_US INT64_C(180000000)
_Static_assert(ART_FRAME_PIXEL_BYTES == UC8179_GRAY4_FRAME_BYTES, "Panel/frame mismatch");
static const char *TAG = "ART_MANAGER";
static art_store_t store;
static uint8_t *buffer;
static SemaphoreHandle_t buffer_lock;
static TaskHandle_t worker, upload_owner;
static uc8179_handle_t panel;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static art_manager_status_t state;
static bool pending;

void art_manager_status(art_manager_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&status_lock);
    *out = state;
    portEXIT_CRITICAL(&status_lock);
}

static void display_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        portENTER_CRITICAL(&status_lock);
        bool due = pending && esp_timer_get_time() >= state.next_refresh_us;
        portEXIT_CRITICAL(&status_lock);
        if (!due || xSemaphoreTake(buffer_lock, 0) != pdTRUE) continue;
        uint32_t generation = store.generation;
        art_store_result_t loaded = art_store_load(&store, buffer, ART_FRAME_PACKET_BYTES);
        esp_err_t err = loaded == ART_STORE_OK ? (panel ? ESP_OK : ESP_ERR_INVALID_STATE) : ESP_FAIL;
        portENTER_CRITICAL(&status_lock);
        pending = false;
        state.store_error = loaded;
        state.display = ART_DISPLAY_BUSY;
        portEXIT_CRITICAL(&status_lock);
        if (err == ESP_OK) {
            portENTER_CRITICAL(&status_lock);
            state.next_refresh_us = esp_timer_get_time() + REFRESH_GAP_US;
            portEXIT_CRITICAL(&status_lock);
            err = uc8179_init_gray4(panel);
            if (err == ESP_OK) err = uc8179_display_gray4(panel, buffer+ART_FRAME_HEADER_BYTES, ART_FRAME_PIXEL_BYTES);
            esp_err_t shutdown = err == ESP_OK ? uc8179_sleep(panel) : uc8179_power_off(panel);
            if (err == ESP_OK) err = shutdown;
            else if (shutdown != ESP_OK) ESP_LOGE(TAG, "Panel power off: %s", esp_err_to_name(shutdown));
        }
        portENTER_CRITICAL(&status_lock);
        state.display_error = err;
        state.display = err == ESP_OK ? ART_DISPLAY_READY : ART_DISPLAY_ERROR;
        if (err == ESP_OK) state.displayed_generation = generation;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI(TAG, "DISPLAY generation=%" PRIu32 " result=%s", generation, esp_err_to_name(err));
        xSemaphoreGive(buffer_lock);
    }
}

esp_err_t art_manager_start(uc8179_handle_t borrowed_panel)
{
    if (buffer_lock) return ESP_ERR_INVALID_STATE;
    buffer_lock = xSemaphoreCreateMutex();
    if (!buffer_lock) return ESP_ERR_NO_MEM;
    buffer = heap_caps_malloc(ART_FRAME_PACKET_BYTES, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!buffer) return ESP_ERR_NO_MEM;
    panel = borrowed_panel;
    art_store_result_t result;
    esp_err_t err = art_store_open_partition(&store, "art", &result);
    state.store_error = result;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Art partition unavailable: %s (store=%d)", esp_err_to_name(err), (int)result);
        return err;
    }
    state.has_frame = result == ART_STORE_OK;
    state.saved_generation = store.generation;
    state.pixel_crc = store.pixel_crc;
    state.display = state.has_frame ? ART_DISPLAY_QUEUED : ART_DISPLAY_EMPTY;
    pending = state.has_frame;
    if (xTaskCreate(display_worker, "art_display", 4096, NULL, 4, &worker) != pdPASS)
        return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&status_lock);
    state.storage_ready = true;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGI(TAG, "STORE %s generation=%" PRIu32, state.has_frame ? "RESTORED" : "EMPTY", store.generation);
    return ESP_OK;
}

esp_err_t art_manager_begin_upload(uint8_t **packet)
{
    if (!packet) return ESP_ERR_INVALID_ARG;
    *packet = NULL;
    art_manager_status_t snapshot;
    art_manager_status(&snapshot);
    if (!snapshot.storage_ready || !buffer_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(buffer_lock, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    upload_owner = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&status_lock);
    state.uploading = true;
    portEXIT_CRITICAL(&status_lock);
    *packet = buffer;
    return ESP_OK;
}

void art_manager_abort_upload(void)
{
    if (!upload_owner || upload_owner != xTaskGetCurrentTaskHandle()) return;
    portENTER_CRITICAL(&status_lock);
    state.uploading = false;
    portEXIT_CRITICAL(&status_lock);
    upload_owner = NULL;
    xSemaphoreGive(buffer_lock);
}

art_store_result_t art_manager_commit_upload(size_t length)
{
    if (!upload_owner || upload_owner != xTaskGetCurrentTaskHandle()) return ART_STORE_ARGUMENT;
    art_frame_view_t frame;
    art_store_result_t result = ART_STORE_INVALID_FRAME;
    if (art_frame_parse(buffer, length, &frame) == ART_FRAME_OK) {
        result = art_store_save(&store, buffer, length);
    }
    portENTER_CRITICAL(&status_lock);
    state.store_error = result;
    if (result == ART_STORE_OK) {
        state.has_frame = true;
        state.saved_generation = store.generation;
        state.pixel_crc = store.pixel_crc;
        if (state.display != ART_DISPLAY_READY || state.displayed_generation != store.generation) {
            state.display = ART_DISPLAY_QUEUED;
            pending = true;
        }
    }
    portEXIT_CRITICAL(&status_lock);
    art_manager_abort_upload();
    if (result == ART_STORE_OK) {
        ESP_LOGI(TAG, "SAVED generation=%" PRIu32 " crc=%08" PRIx32, store.generation, store.pixel_crc);
        xTaskNotifyGive(worker);
    }
    return result;
}
