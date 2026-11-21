#include "art_manager.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "art_store_esp.h"
#include "home_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
static home_ui_t ui;
static home_live_data_t *live,*render_live;
static bool (*submit_home)(const home_command_t *);
static bool mono_baseline;
static unsigned partials;
/* Keep the prototype's previously exercised 30-partial UI budget.
 * Vendor recommendation is five; verify ghosting on the physical UI. */
#define UI_PARTIAL_LIMIT 30U
_Static_assert(ART_FRAME_PACKET_BYTES >= 2*HOME_UI_BYTES,"UI buffers must fit the packet allocation");

/* Call under status_lock. Photo payload is not kept in RAM while menu is open:
 * the same buffer holds the 48 KB working frame and 48 KB shown baseline. */
static void publish_ui(void)
{
    state.menu_requested=ui.active;
    state.ui_revision=ui.revision;
    state.ui_shown_revision=ui.shown_revision;
}

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
        if(home_ui_tick(&ui,esp_timer_get_time())){pending=true;state.display=ART_DISPLAY_QUEUED;publish_ui();}
        bool due = pending;
        portEXIT_CRITICAL(&status_lock);
        if (!due || xSemaphoreTake(buffer_lock, 0) != pdTRUE) continue;
        uint32_t generation = store.generation;
        portENTER_CRITICAL(&status_lock);
        home_ui_t requested=ui;
        if(live&&render_live){*render_live=*live;requested.live=render_live;}
        pending = false;
        state.display = ART_DISPLAY_BUSY;
        portEXIT_CRITICAL(&status_lock);
        esp_err_t err=panel?ESP_OK:ESP_ERR_INVALID_STATE;
        int64_t started=esp_timer_get_time();
        bool menu=requested.active;
        if(err==ESP_OK&&menu){
            uint8_t *shown=buffer+HOME_UI_BYTES;
            if(!home_ui_render(buffer,HOME_UI_BYTES,&requested))err=ESP_ERR_INVALID_ARG;
            home_ui_region_t region={0};
            bool dirty=!mono_baseline||home_ui_changed_region(shown,buffer,HOME_UI_BYTES,&region);
            bool full=!mono_baseline||(dirty&&partials>=UI_PARTIAL_LIMIT);
            if(err==ESP_OK&&dirty){
                ESP_LOGI(TAG,"UI %s revision=%" PRIu32,full?"FULL":"PARTIAL",requested.revision);
                if(full){
                    err=uc8179_init(panel);
                    if(err==ESP_OK)err=uc8179_display_frame(panel,buffer,HOME_UI_BYTES);
                    if(err==ESP_OK)err=uc8179_prepare_partial(panel,buffer,HOME_UI_BYTES);
                    if(err==ESP_OK){mono_baseline=true;partials=0;}
                }else{
                    err=uc8179_display_partial_region(panel,buffer,HOME_UI_BYTES,region.x,region.y,region.width,region.height);
                    if(err==ESP_OK)++partials;
                }
                esp_err_t off=uc8179_power_off(panel);
                if(err==ESP_OK)err=off;
            }
            if(err==ESP_OK)memcpy(shown,buffer,HOME_UI_BYTES);
        }else if(err==ESP_OK){
            mono_baseline=false;partials=0;
            art_store_result_t loaded=art_store_load(&store,buffer,ART_FRAME_PACKET_BYTES);
            portENTER_CRITICAL(&status_lock);
            state.store_error=loaded;
            portEXIT_CRITICAL(&status_lock);
            if(loaded==ART_STORE_OK){
                err=uc8179_init_gray4(panel);
                if(err==ESP_OK)err=uc8179_display_gray4(panel,buffer+ART_FRAME_HEADER_BYTES,ART_FRAME_PIXEL_BYTES);
            }else if(loaded==ART_STORE_EMPTY){
                if(!home_ui_render_empty_art(buffer,HOME_UI_BYTES))err=ESP_ERR_INVALID_ARG;
                if(err==ESP_OK)err=uc8179_init(panel);
                if(err==ESP_OK)err=uc8179_display_frame(panel,buffer,HOME_UI_BYTES);
            }else err=ESP_FAIL;
            esp_err_t shutdown=err==ESP_OK?uc8179_sleep(panel):uc8179_power_off(panel);
            if(err==ESP_OK)err=shutdown;
            else if(shutdown!=ESP_OK)ESP_LOGE(TAG,"Panel power off: %s",esp_err_to_name(shutdown));
        }
        if(err!=ESP_OK){mono_baseline=false;partials=0;}
        portENTER_CRITICAL(&status_lock);
        state.display_error = err;
        if(err==ESP_OK){
            state.menu_visible=menu;
            home_ui_presented(&ui,menu,requested.revision,esp_timer_get_time());
            if(!menu&&state.has_frame)state.displayed_generation=generation;
        }else ui.visible=false;
        publish_ui();
        state.display=pending?ART_DISPLAY_QUEUED:err==ESP_OK?ART_DISPLAY_READY:ART_DISPLAY_ERROR;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI(TAG,"DISPLAY mode=%s generation=%" PRIu32 " ui=%" PRIu32 " result=%s time=%" PRIi64 "ms",
                 menu?"home":"art",generation,requested.revision,esp_err_to_name(err),(esp_timer_get_time()-started)/1000);
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
    home_ui_init(&ui);
    publish_ui();
    state.display = state.has_frame ? ART_DISPLAY_QUEUED : ART_DISPLAY_EMPTY;
    pending = true;
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
    /* Receiving even an aborted upload overwrites both monochrome buffers. */
    mono_baseline=false;
    portENTER_CRITICAL(&status_lock);
    state.uploading = true;
    ui.visible=false;
    home_ui_cancel_touch(&ui);
    portEXIT_CRITICAL(&status_lock);
    *packet = buffer;
    return ESP_OK;
}

void art_manager_abort_upload(void)
{
    if (!upload_owner || upload_owner != xTaskGetCurrentTaskHandle()) return;
    portENTER_CRITICAL(&status_lock);
    state.uploading = false;
    if(ui.active){pending=true;state.display=ART_DISPLAY_QUEUED;}
    portEXIT_CRITICAL(&status_lock);
    upload_owner = NULL;
    xSemaphoreGive(buffer_lock);
    if(worker)xTaskNotifyGive(worker);
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
        home_ui_show_art(&ui);
        publish_ui();
        state.display=ART_DISPLAY_QUEUED;
        pending=true;
    }
    portEXIT_CRITICAL(&status_lock);
    art_manager_abort_upload();
    if (result == ART_STORE_OK) {
        ESP_LOGI(TAG, "SAVED generation=%" PRIu32 " crc=%08" PRIx32, store.generation, store.pixel_crc);
        xTaskNotifyGive(worker);
    }
    return result;
}

void art_manager_touch(unsigned count,int x,int y,unsigned id,int64_t now)
{
    portENTER_CRITICAL(&status_lock);
    bool dirty=false;
    if(!state.storage_ready||state.uploading)home_ui_cancel_touch(&ui);
    else if(state.display==ART_DISPLAY_BUSY){
        bool had=ui.contact;
        home_ui_cancel_touch(&ui);ui.contact=count>0;ui.require_release=count>0;
        if(count||had)ui.last_activity_us=now;
    }
    else if(home_ui_touch(&ui,count,x,y,id,now)){
        pending=true;state.display=ART_DISPLAY_QUEUED;dirty=true;publish_ui();
    }
    home_command_t command=ui.command;ui.command.kind=HOME_CMD_NONE;
    portEXIT_CRITICAL(&status_lock);
    if(command.kind!=HOME_CMD_NONE&&submit_home&&!submit_home(&command)){
        home_live_data_t *data=live;
        if(data){portENTER_CRITICAL(&status_lock);
            snprintf(data->message,sizeof(data->message),"Подождите завершения запроса");
            ++ui.revision;pending=ui.active;publish_ui();portEXIT_CRITICAL(&status_lock);}
    }
    if(dirty&&worker)xTaskNotifyGive(worker);
}

void art_manager_cancel_touch(void)
{
    portENTER_CRITICAL(&status_lock);home_ui_cancel_touch(&ui);portEXIT_CRITICAL(&status_lock);
}

void art_manager_open_menu(void)
{
    portENTER_CRITICAL(&status_lock);
    bool ready=state.storage_ready&&!state.uploading;
    if(ready){home_ui_open_menu(&ui,esp_timer_get_time());pending=true;state.display=ART_DISPLAY_QUEUED;publish_ui();}
    portEXIT_CRITICAL(&status_lock);
    if(ready&&worker)xTaskNotifyGive(worker);
}

void art_manager_show_art(void)
{
    portENTER_CRITICAL(&status_lock);
    bool ready=state.storage_ready&&!state.uploading;
    if(ready){home_ui_show_art(&ui);pending=true;state.display=ART_DISPLAY_QUEUED;publish_ui();}
    portEXIT_CRITICAL(&status_lock);
    if(ready&&worker)xTaskNotifyGive(worker);
}

esp_err_t art_manager_enable_home(bool (*submit)(const home_command_t *))
{
    if(live)return ESP_ERR_INVALID_STATE;
    home_live_data_t *model=heap_caps_malloc(sizeof(*model),MALLOC_CAP_8BIT);
    home_live_data_t *snapshot=heap_caps_malloc(sizeof(*snapshot),MALLOC_CAP_8BIT);
    if(!model||!snapshot){free(model);free(snapshot);return ESP_ERR_NO_MEM;}
    memset(model,0,sizeof(*model));model->epoch=1;
    snprintf(model->message,sizeof(model->message),"Войдите в Яндекс в настройках");
    portENTER_CRITICAL(&status_lock);live=model;render_live=snapshot;submit_home=submit;home_ui_set_live(&ui,live);publish_ui();portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}
void art_manager_home_update(const home_live_data_t *data)
{
    if(!data||!live)return;
    bool notify=false;
    portENTER_CRITICAL(&status_lock);
    if(memcmp(live,data,sizeof(*live))){
        bool reset=live->epoch!=data->epoch;
        *live=*data;
        if(reset)ui.live=NULL;
        home_ui_set_live(&ui,live);
        if(ui.active){pending=true;notify=true;if(state.display!=ART_DISPLAY_BUSY)state.display=ART_DISPLAY_QUEUED;}
        publish_ui();
    }
    portEXIT_CRITICAL(&status_lock);
    if(notify&&worker)xTaskNotifyGive(worker);
}
