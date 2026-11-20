#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "art_manager.h"
#include "art_store_esp.h"
#include "freertos/semphr.h"

static uint8_t flash[ART_STORE_MIN_BYTES],packet[ART_FRAME_PACKET_BYTES],pixels[ART_FRAME_PIXEL_BYTES];
static void (*worker_fn)(void *);
static void *current_task=(void *)1,*mutex_owner;
static jmp_buf loop_exit;
static unsigned notification_calls,refreshes,sleeps,power_offs,mono_full,mono_partial;
static int64_t now;
static bool fail_read,fail_write,fail_panel;
static bool release_during_mono;
static art_store_io_t io;

const char *esp_err_to_name(esp_err_t err) { return err==ESP_OK?"ESP_OK":"ESP_FAIL"; }
int64_t esp_timer_get_time(void) { return now; }
void *heap_caps_malloc(size_t n,unsigned caps)
{ assert(n==ART_FRAME_PACKET_BYTES&&caps==(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));return calloc(1,n); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)10; }
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t timeout)
{ (void)s;assert(!timeout);if(mutex_owner)return pdFALSE;mutex_owner=current_task;return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s;assert(mutex_owner==current_task);mutex_owner=NULL;return pdTRUE; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
int xTaskCreate(void (*task)(void *),const char *name,uint32_t stack,void *arg,unsigned priority,TaskHandle_t *handle)
{ (void)name;(void)stack;(void)arg;(void)priority;worker_fn=task;*handle=(void *)2;return pdPASS; }
uint32_t ulTaskNotifyTake(int clear,TickType_t wait)
{ (void)clear;(void)wait;if(notification_calls++)longjmp(loop_exit,1);return 0; }
void xTaskNotifyGive(TaskHandle_t task) { assert(task==(void *)2); }
static void run_worker_once(void)
{
    notification_calls=0;current_task=(void *)2;
    if(!setjmp(loop_exit))worker_fn(NULL);
    current_task=(void *)1;
}
static bool read_bytes(void *ctx,uint32_t at,void *out,size_t n)
{ (void)ctx;assert(at+n<=sizeof(flash));if(fail_read)return false;memcpy(out,flash+at,n);return true; }
static bool write_bytes(void *ctx,uint32_t at,const void *data,size_t n)
{
    (void)ctx;assert(at+n<=sizeof(flash));if(fail_write)return false;
    const uint8_t *src=data;for(size_t i=0;i<n;++i){assert((flash[at+i]&src[i])==src[i]);flash[at+i]&=src[i];}return true;
}
static bool erase_bytes(void *ctx,uint32_t at,size_t n)
{ (void)ctx;assert(at+n<=sizeof(flash));memset(flash+at,255,n);return true; }
esp_err_t art_store_open_partition(art_store_t *store,const char *label,art_store_result_t *result)
{ assert(!strcmp(label,"art"));*result=art_store_open(store,&io);return *result==ART_STORE_IO?ESP_FAIL:ESP_OK; }
esp_err_t uc8179_init_gray4(uc8179_handle_t panel)
{ assert(panel==(void *)3);return fail_panel?ESP_ERR_TIMEOUT:ESP_OK; }
esp_err_t uc8179_display_gray4(uc8179_handle_t panel,const uint8_t *data,size_t n)
{
    assert(panel==(void *)3&&n==sizeof(pixels)&&!memcmp(data,pixels,n));
    art_manager_status_t s;art_manager_status(&s);assert(s.display==ART_DISPLAY_BUSY);
    uint8_t *lease;assert(art_manager_begin_upload(&lease)==ESP_ERR_TIMEOUT&&!lease);
    ++refreshes;return ESP_OK;
}
esp_err_t uc8179_sleep(uc8179_handle_t panel) { (void)panel;++sleeps;return ESP_OK; }
esp_err_t uc8179_power_off(uc8179_handle_t panel) { (void)panel;++power_offs;return ESP_OK; }
esp_err_t uc8179_init(uc8179_handle_t panel) { assert(panel==(void *)3);return ESP_OK; }
esp_err_t uc8179_display_frame(uc8179_handle_t panel,const uint8_t *data,size_t n)
{ (void)data;assert(panel==(void *)3&&n==48000);++mono_full;
  if(release_during_mono){release_during_mono=false;art_manager_touch(0,-1,-1,0,now);}
  return ESP_OK; }
esp_err_t uc8179_prepare_partial(uc8179_handle_t panel,const uint8_t *data,size_t n)
{ (void)data;assert(panel==(void *)3&&n==48000);return ESP_OK; }
esp_err_t uc8179_display_partial_region(uc8179_handle_t panel,const uint8_t *data,size_t n,
                                       uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{ (void)data;assert(panel==(void *)3&&n==48000&&w&&h&&x+w<=800&&y+h<=480);++mono_partial;return ESP_OK; }
static art_store_result_t upload(void)
{
    uint8_t *lease;assert(art_manager_begin_upload(&lease)==ESP_OK);
    memcpy(lease,packet,sizeof(packet));return art_manager_commit_upload(sizeof(packet));
}
int main(int argc,char **argv)
{
    memset(flash,255,sizeof(flash));memset(pixels,0x1b,sizeof(pixels));
    io=(art_store_io_t){.size=sizeof(flash),.read=read_bytes,.write=write_bytes,.erase=erase_bytes};
    assert(art_frame_encode(packet,sizeof(packet),pixels,sizeof(pixels))==ART_FRAME_OK);
    if(argc>1&&(!strcmp(argv[1],"restore")||!strcmp(argv[1],"ui"))) {
        art_store_t initial;assert(art_store_open(&initial,&io)==ART_STORE_EMPTY);
        assert(art_store_save(&initial,packet,sizeof(packet))==ART_STORE_OK);
        assert(art_manager_start((void *)3)==ESP_OK);run_worker_once();
        art_manager_status_t s;art_manager_status(&s);
        assert(s.has_frame&&s.saved_generation==1&&s.displayed_generation==1&&refreshes==1);
        if(!strcmp(argv[1],"ui")){
            release_during_mono=true;art_manager_touch(1,100,200,0,now);run_worker_once();
            art_manager_status(&s);assert(s.menu_visible&&mono_full==1&&refreshes==1);
            art_manager_touch(1,100,200,0,now);art_manager_touch(0,-1,-1,0,now);run_worker_once();
            assert(mono_partial==1&&mono_full==1); /* Toggle in the displayed HOME. */
            for(unsigned i=0;i<29;++i){
                art_manager_touch(1,100,200,0,now);art_manager_touch(0,-1,-1,0,now);run_worker_once();
            }
            assert(mono_partial==30&&mono_full==1);
            art_manager_touch(1,100,450,0,now);art_manager_touch(0,-1,-1,0,now);run_worker_once();
            assert(mono_partial==30&&mono_full==1); /* No-op tab never triggers a refresh. */
            art_manager_touch(1,100,200,0,now);art_manager_touch(0,-1,-1,0,now);run_worker_once();
            assert(mono_partial==30&&mono_full==2); /* Preserve the established prototype budget. */
            /* An interrupted upload destroys the RAM baseline, but not photo. */
            uint8_t *p;assert(art_manager_begin_upload(&p)==ESP_OK);memset(p,0xcc,1000);art_manager_abort_upload();
            run_worker_once();assert(mono_full==3);
            now+=45000000;run_worker_once();art_manager_status(&s);
            assert(!s.menu_visible&&s.displayed_generation==1&&refreshes==2);
            art_manager_open_menu();run_worker_once();assert(mono_full==4);
            assert(upload()==ART_STORE_OK);run_worker_once();art_manager_status(&s);
            assert(!s.menu_visible&&s.saved_generation==2&&s.displayed_generation==2&&refreshes==3);
            puts("PASS: actual manager Art -> full UI -> partial toggle -> aborted upload rebase -> 45s same-photo restore -> uploaded photo exit.");
            return 0;
        }
        puts("PASS: actual ArtManager restores and displays a committed Flash frame at boot.");return 0;
    }
    assert(art_manager_start((void *)3)==ESP_OK);
    art_manager_status_t s;art_manager_status(&s);assert(s.storage_ready&&!s.has_frame);
    uint8_t *lease;assert(art_manager_begin_upload(&lease)==ESP_OK);
    memcpy(lease,packet,100);run_worker_once();assert(!refreshes);
    current_task=(void *)2;art_manager_abort_upload();
    assert(art_manager_commit_upload(sizeof(packet))==ART_STORE_ARGUMENT);
    current_task=(void *)1;art_manager_abort_upload();
    assert(!mutex_owner);
    assert(art_manager_begin_upload(&lease)==ESP_OK);memcpy(lease,packet,sizeof(packet));lease[1000]^=1;
    assert(art_manager_commit_upload(sizeof(packet))==ART_STORE_INVALID_FRAME&&!mutex_owner);
    art_manager_status(&s);assert(!s.has_frame&&!s.saved_generation);
    assert(upload()==ART_STORE_OK);
    art_manager_status(&s);assert(s.has_frame&&s.saved_generation==1&&!s.displayed_generation&&s.display==ART_DISPLAY_QUEUED);
    /* A receiving HTTP task owns the only buffer, so queued display waits. */
    assert(art_manager_begin_upload(&lease)==ESP_OK);run_worker_once();assert(!refreshes);
    art_manager_abort_upload();run_worker_once();
    art_manager_status(&s);assert(s.display==ART_DISPLAY_READY&&s.displayed_generation==1&&refreshes==1&&sleeps==1);
    fail_write=true;assert(upload()==ART_STORE_IO);fail_write=false;
    art_manager_status(&s);assert(s.saved_generation==1&&s.displayed_generation==1&&!mutex_owner);
    /* Second photo is displayed at the same clock time: no cooldown timer. */
    assert(upload()==ART_STORE_OK);run_worker_once();assert(refreshes==2);
    art_manager_status(&s);assert(s.saved_generation==2&&s.displayed_generation==2);
    fail_panel=true;assert(upload()==ART_STORE_OK);run_worker_once();
    art_manager_status(&s);assert(s.saved_generation==3&&s.displayed_generation==2&&s.display==ART_DISPLAY_ERROR&&power_offs==1);
    run_worker_once();assert(refreshes==2); /* No automatic fault loop. */
    fail_panel=false;assert(upload()==ART_STORE_OK);run_worker_once();
    art_manager_status(&s);assert(s.saved_generation==4&&s.displayed_generation==4&&refreshes==3&&sleeps==3);
    for(unsigned i=0;i<3;++i){assert(upload()==ART_STORE_OK);run_worker_once();}
    art_manager_status(&s);assert(s.saved_generation==7&&s.displayed_generation==7&&refreshes==6);
    puts("PASS: actual ArtManager upload/display ownership, abort/CRC/save failures, immediate consecutive photos, saved vs shown generations and retry.");
    return 0;
}
