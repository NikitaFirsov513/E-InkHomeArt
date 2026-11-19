#include "test_sdk.h"
#include <stdlib.h>
#include <string.h>
#include "uc8179.h"
#include "gt911.h"
#include "lis2dw12_idf.h"
#include "touch_test.h"
#include "frame_pipeline.h"
#include "art_frame.h"
#include "art_store.h"

/* No CRT or device is required by the test DLL. Only the SDK boundary is
 * simulated; uc8179.c, gt911.c, lis2dw12_idf.c and the ST driver are real code. */
int _fltused;
static union { max_align_t align; uint8_t bytes[8192]; } heap[8];
static bool allocated[8];
static unsigned allocation_calls, allocation_fail_at, allocation_count;
static bool invalid_free;
static const char *failure;
#define CHECK(condition) do { if (!(condition)) { failure = #condition; return __LINE__; } } while (0)

void *memset(void *dest, int value, size_t size)
{ uint8_t *p = dest; for (size_t i = 0; i < size; ++i) p[i] = (uint8_t)value; return dest; }
void *memcpy(void *dest, const void *source, size_t size)
{ uint8_t *d = dest; const uint8_t *s = source; for (size_t i = 0; i < size; ++i) d[i] = s[i]; return dest; }
int memcmp(const void *a, const void *b, size_t size)
{ const uint8_t *x = a, *y = b; for (size_t i = 0; i < size; ++i) if (x[i] != y[i]) return x[i]-y[i]; return 0; }
void *calloc(size_t count, size_t size)
{
    ++allocation_calls;
    if (allocation_calls == allocation_fail_at || !count || size > 8192/count) return NULL;
    for (unsigned i = 0; i < 8; ++i) if (!allocated[i]) {
        allocated[i] = true; ++allocation_count;
        return memset(heap[i].bytes,0,count*size);
    }
    return NULL;
}
void free(void *pointer)
{
    if (!pointer) return;
    for (unsigned i = 0; i < 8; ++i) if (pointer == heap[i].bytes && allocated[i]) {
        allocated[i] = false; --allocation_count; return;
    }
    invalid_free = true;
}
void *heap_caps_malloc(size_t size, unsigned caps)
{
    if (caps != (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)) return NULL;
    return calloc(1,size);
}

static int64_t clock_us;
static unsigned pin_level[49], pin_mode[49], pin_interrupt[49];
static int busy_level = 1;
static bool busy_after_refresh;
static unsigned address_latched;
void vTaskDelay(TickType_t ticks) { clock_us += (int64_t)ticks*10000; }
int64_t esp_timer_get_time(void) { return clock_us; }
void esp_rom_delay_us(uint32_t delay) { clock_us += delay; }
esp_err_t gpio_config(const gpio_config_t *config)
{
    for (unsigned i = 0; i < 49; ++i) if (config->pin_bit_mask & (UINT64_C(1)<<i)) {
        pin_mode[i] = config->mode; pin_interrupt[i] = config->intr_type;
    }
    return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    if (!GPIO_IS_VALID_GPIO(pin)) return ESP_ERR_INVALID_ARG;
    pin_level[pin] = level;
    if (pin == 12 && level) address_latched = pin_level[9];
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) { return pin == 13 ? busy_level : (int)pin_level[pin]; }
esp_err_t gpio_set_direction(gpio_num_t pin, int mode) { pin_mode[pin] = mode; return ESP_OK; }
esp_err_t gpio_intr_disable(gpio_num_t pin) { pin_interrupt[pin] = GPIO_INTR_DISABLE; return ESP_OK; }
esp_err_t gpio_reset_pin(gpio_num_t pin) { pin_mode[pin] = 0; pin_interrupt[pin] = 0; return ESP_OK; }

static bool fail_spi_add, fail_spi_remove;
static bool spi_driver_ready;
static unsigned spi_calls, spi_fail_at, spi_devices;
static size_t bus_max, largest_chunk, old_size, new_size;
static uint8_t spi_command, old_frame[UC8179_FRAME_BYTES], new_frame[UC8179_FRAME_BYTES];
static uint8_t spi_settings[256][10];
static unsigned spi_command_count[256];
esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t host, size_t *length)
{
    (void)host;
    /* IDF 6.1 creates the SPI master context lazily on first device attach. */
    if (!spi_driver_ready || !length) return ESP_ERR_INVALID_ARG;
    *length = bus_max;
    return ESP_OK;
}
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *config, spi_device_handle_t *out)
{
    (void)host;
    if (fail_spi_add) return ESP_ERR_NO_MEM;
    if (config->mode || config->spics_io_num != 47) return ESP_ERR_INVALID_ARG;
    *out = (void *)(uintptr_t)0x123; ++spi_devices; spi_driver_ready = true; return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t device)
{ (void)device; if (fail_spi_remove) return ESP_ERR_INVALID_STATE; --spi_devices; return ESP_OK; }
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *tx)
{
    (void)device;
    if (++spi_calls == spi_fail_at) return ESP_ERR_TIMEOUT;
    if (tx->flags & SPI_TRANS_USE_TXDATA) {
        spi_command = tx->tx_data[0];
        ++spi_command_count[spi_command];
        if (spi_command == 0x12 && busy_after_refresh) busy_level = 0;
        if (spi_command == 0x10) old_size = 0;
        if (spi_command == 0x13) new_size = 0;
        return tx->length == 8 ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    size_t length = tx->length/8;
    if (length > largest_chunk) largest_chunk = length;
    if (length > bus_max || !tx->tx_buffer) return ESP_ERR_INVALID_SIZE;
    if (spi_command == 0x10 || spi_command == 0x13) {
        size_t *size = spi_command == 0x10 ? &old_size : &new_size;
        uint8_t *frame = spi_command == 0x10 ? old_frame : new_frame;
        if (*size + length > UC8179_FRAME_BYTES) return ESP_ERR_INVALID_SIZE;
        memcpy(frame + *size,tx->tx_buffer,length); *size += length;
    } else if (length <= sizeof(spi_settings[0])) memcpy(spi_settings[spi_command],tx->tx_buffer,length);
    return ESP_OK;
}

static unsigned i2c_devices, i2c_calls;
static bool fail_i2c, reset_stuck;
static uint8_t registers[256];
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config, i2c_master_dev_handle_t *out)
{
    if (!bus || config->device_address != 0x19) return ESP_ERR_INVALID_ARG;
    *out = (void *)(uintptr_t)0x456; ++i2c_devices; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{ (void)device; --i2c_devices; return ESP_OK; }
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *reg, size_t reg_length,
                                    uint8_t *data, size_t length, int timeout)
{
    (void)device; ++i2c_calls;
    if (fail_i2c) return ESP_ERR_TIMEOUT;
    if (reg_length != 1 || timeout <= 0 || (unsigned)*reg + length > 256) return ESP_ERR_INVALID_ARG;
    if (*reg == LIS2DW12_CTRL2 && !reset_stuck) registers[LIS2DW12_CTRL2] &= (uint8_t)~0x40U;
    memcpy(data,registers+*reg,length);
    if (*reg == LIS2DW12_OUT_X_L && length == 6) registers[LIS2DW12_STATUS] &= (uint8_t)~1U;
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t length, int timeout)
{
    (void)device; ++i2c_calls;
    if (fail_i2c) return ESP_ERR_TIMEOUT;
    if (timeout <= 0 || length < 2 || (unsigned)data[0]+length-1 > 256) return ESP_ERR_INVALID_ARG;
    memcpy(registers+data[0],data+1,length-1); return ESP_OK;
}

static uint16_t gt_width, gt_height, gt_x, gt_y;
static bool gt_bad_id, gt_rx_fail, gt_ready;
static uint8_t gt_samples, gt_cached;
static unsigned gt_reads, gt_sleep_calls;
static int gt_timeout;
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus, const esp_lcd_panel_io_i2c_config_t *config,
                                 esp_lcd_panel_io_handle_t *out)
{ (void)bus; gt_timeout = config->transaction_timeout_ms; *out = calloc(1,1); return *out ? ESP_OK : ESP_ERR_NO_MEM; }
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) { free(io); return ESP_OK; }
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io, int reg, void *data, size_t length)
{
    (void)io;
    if (gt_rx_fail) return ESP_ERR_TIMEOUT;
    uint8_t *out = data;
    if (reg == 0x8048 && length == 4) {
        out[0] = (uint8_t)gt_width; out[1] = (uint8_t)(gt_width>>8);
        out[2] = (uint8_t)gt_height; out[3] = (uint8_t)(gt_height>>8); return ESP_OK;
    }
    if (reg == 0x8140 && length == 4) { memcpy(out,gt_bad_id ? "000" : "911",4); return ESP_OK; }
    if (reg == 0x814E && length == 1) { *out = (gt_ready ? 0x80 : 0) | gt_samples; return ESP_OK; }
    return ESP_ERR_INVALID_ARG;
}
esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config,
                                   esp_lcd_touch_handle_t *out)
{
    (void)io; *out = calloc(1,sizeof(**out));
    if (!*out) return ESP_ERR_NO_MEM;
    (*out)->config = *config; return ESP_OK;
}
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t touch) { free(touch); return ESP_OK; }
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t touch)
{
    (void)touch; ++gt_reads;
    if (gt_ready) { gt_cached = gt_samples; gt_ready = false; }
    return ESP_OK; /* Actual upstream keeps its old points when no frame is ready. */
}
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t touch, esp_lcd_touch_point_data_t *points,
                               uint8_t *count, uint8_t max_points)
{
    *count = gt_cached < max_points ? gt_cached : max_points;
    for (unsigned i = 0; i < *count; ++i) {
        uint16_t x = gt_x+i, y = gt_y;
        if (touch->config.flags.mirror_x) x = touch->config.x_max-x;
        if (touch->config.flags.mirror_y) y = touch->config.y_max-y;
        if (touch->config.flags.swap_xy) { uint16_t temp = x; x = y; y = temp; }
        points[i] = (esp_lcd_touch_point_data_t){ .x=x, .y=y, .track_id=(uint8_t)i, .strength=10 };
    }
    return ESP_OK;
}
esp_err_t esp_lcd_touch_enter_sleep(esp_lcd_touch_handle_t touch) { (void)touch; ++gt_sleep_calls; return ESP_OK; }

static uc8179_config_t epd_config(void)
{ return (uc8179_config_t){ .host=SPI2_HOST,.cs_pin=47,.dc_pin=21,.reset_pin=14,.busy_pin=13,.clock_speed_hz=1000000,.busy_timeout_ms=50 }; }
static gt911_config_t touch_config(void)
{ return (gt911_config_t){ .bus=(void *)(uintptr_t)1,.reset_pin=12,.interrupt_pin=9,.address=GT911_ADDRESS_LOW,.clock_speed_hz=100000 }; }
static lis2dw12_idf_config_t lis_config(void)
{ return (lis2dw12_idf_config_t){ .bus=(void *)(uintptr_t)1,.address=0x19,.clock_speed_hz=100000,.timeout_ms=100 }; }

static int test_epd_alloc_failures(void)
{
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev = NULL;
    for (unsigned fail = 1; fail <= 2; ++fail) {
        allocation_calls = 0; allocation_fail_at = fail;
        CHECK(uc8179_new(&cfg,&dev) == ESP_ERR_NO_MEM);
        CHECK(!dev && allocation_count == 0 && spi_devices == 0);
    }
    allocation_fail_at = 0; fail_spi_add = true;
    CHECK(uc8179_new(&cfg,&dev) == ESP_ERR_NO_MEM);
    CHECK(allocation_count == 0 && !invalid_free);
    fail_spi_add = false; bus_max = 0;
    CHECK(uc8179_new(&cfg,&dev) == ESP_ERR_INVALID_ARG);
    CHECK(!dev && allocation_count == 0 && spi_devices == 0 && !invalid_free);
    return 0;
}
static int test_epd_busy_timeout(void)
{
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev;
    CHECK(uc8179_new(&cfg,&dev) == ESP_OK);
    busy_level = 0;
    CHECK(uc8179_wait_idle(dev) == ESP_ERR_TIMEOUT);
    CHECK(clock_us >= 50000 && clock_us < 100000);
    CHECK(uc8179_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}
static int test_epd_frame_and_recovery(void)
{
    static uint8_t frame[UC8179_FRAME_BYTES];
    for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)(i*13);
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev;
    bus_max = 128;
    CHECK(uc8179_new(&cfg,&dev) == ESP_OK);
    CHECK(uc8179_display_frame(dev,frame,sizeof(frame)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_init(dev) == ESP_OK);
    CHECK(uc8179_display_frame(dev,frame,sizeof(frame)-1) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_frame(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(old_size == sizeof(frame) && new_size == sizeof(frame) && largest_chunk <= 128);
    CHECK(memcmp(frame,new_frame,sizeof(frame)) == 0);
    for (unsigned i = 0; i < sizeof(frame); ++i) CHECK(old_frame[i] == 0);
    spi_fail_at = spi_calls + 2;
    CHECK(uc8179_fill(dev,0xA5) == ESP_ERR_TIMEOUT);
    CHECK(uc8179_fill(dev,0xA5) == ESP_ERR_INVALID_STATE);
    spi_fail_at = 0;
    CHECK(uc8179_init(dev) == ESP_OK && uc8179_fill(dev,0xA5) == ESP_OK);
    for (unsigned i = 0; i < sizeof(frame); ++i) CHECK(new_frame[i] == 0xA5);
    CHECK(uc8179_sleep(dev) == ESP_OK);
    CHECK(uc8179_fill(dev,0) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_init(dev) == ESP_OK);
    fail_spi_remove = true;
    CHECK(uc8179_delete(dev) == ESP_ERR_INVALID_STATE && allocation_count == 2);
    fail_spi_remove = false;
    CHECK(uc8179_delete(dev) == ESP_OK && allocation_count == 0 && spi_devices == 0);
    return 0;
}
static int test_epd_gray4_planes(void)
{
    static uint8_t pixels[UC8179_GRAY4_FRAME_BYTES];
    static uint8_t mono[UC8179_FRAME_BYTES];
    /* All 256 possible packed bytes, then an asymmetric final pixel. */
    for (unsigned i = 0; i < sizeof(pixels); ++i) pixels[i] = (uint8_t)(i*37);
    pixels[sizeof(pixels)-1] = 0x1b;
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev;
    bus_max = 128;
    CHECK(uc8179_new(&cfg,&dev) == ESP_OK);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_init_gray4(dev) == ESP_OK);
    const uint8_t interval[] = {0x10,0x07}, booster[] = {0x27,0x27,0x18,0x17};
    CHECK(spi_settings[0x00][0] == 0x1f && spi_settings[0xe0][0] == 2 && spi_settings[0xe5][0] == 0x5f);
    CHECK(memcmp(spi_settings[0x50],interval,sizeof(interval)) == 0);
    CHECK(memcmp(spi_settings[0x06],booster,sizeof(booster)) == 0);
    unsigned calls = spi_calls;
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)-1) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)+1) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_gray4(dev,NULL,sizeof(pixels)) == ESP_ERR_INVALID_ARG && spi_calls == calls);
    CHECK(uc8179_display_frame(dev,mono,sizeof(mono)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_fill(dev,0) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_prepare_partial(dev,mono,sizeof(mono)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_OK);
    CHECK(old_size == UC8179_FRAME_BYTES && new_size == UC8179_FRAME_BYTES && largest_chunk <= bus_max);
    const uint8_t old_bit[] = {1,0,1,0}, new_bit[] = {1,1,0,0};
    for (unsigned pixel = 0; pixel < UC8179_WIDTH*UC8179_HEIGHT; ++pixel) {
        unsigned level = (pixels[pixel/4] >> (6-2*(pixel%4))) & 3;
        unsigned shift = 7-pixel%8;
        CHECK(((old_frame[pixel/8] >> shift) & 1) == old_bit[level]);
        CHECK(((new_frame[pixel/8] >> shift) & 1) == new_bit[level]);
    }
    uc8179_update_stats_t stats;
    CHECK(uc8179_get_update_stats(dev,&stats) == ESP_OK && stats.bytes == 96000);
    CHECK(uc8179_display_partial(dev,mono,sizeof(mono)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_sleep(dev) == ESP_OK);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_init(dev) == ESP_OK && uc8179_display_frame(dev,mono,sizeof(mono)) == ESP_OK);
    CHECK(uc8179_prepare_partial(dev,mono,sizeof(mono)) == ESP_OK);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_init_gray4(dev) == ESP_OK);
    CHECK(uc8179_display_partial(dev,mono,sizeof(mono)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_sleep(dev) == ESP_OK && uc8179_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}

static int test_epd_gray4_recovery(void)
{
    static uint8_t pixels[UC8179_GRAY4_FRAME_BYTES];
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev;
    CHECK(uc8179_init_gray4(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_gray4(NULL,pixels,sizeof(pixels)) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_new(&cfg,&dev) == ESP_OK);
    busy_level = 0;
    CHECK(uc8179_init_gray4(dev) == ESP_ERR_TIMEOUT);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    busy_level = 1;
    CHECK(uc8179_power_off(dev) == ESP_OK);
    spi_fail_at = spi_calls+2;
    CHECK(uc8179_init_gray4(dev) == ESP_ERR_TIMEOUT);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    spi_fail_at = 0;
    CHECK(uc8179_init_gray4(dev) == ESP_OK);
    spi_fail_at = spi_calls+2; /* First OLD data chunk. No refresh may follow. */
    unsigned refreshes = spi_command_count[0x12];
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_TIMEOUT);
    CHECK(spi_command_count[0x12] == refreshes);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    spi_fail_at = 0;
    CHECK(uc8179_power_off(dev) == ESP_OK && uc8179_init_gray4(dev) == ESP_OK);
    busy_after_refresh = true;
    int64_t start = clock_us;
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_TIMEOUT);
    CHECK(clock_us-start >= 50000 && clock_us-start < 100000);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_ERR_INVALID_STATE);
    busy_after_refresh = false; busy_level = 1;
    CHECK(uc8179_power_off(dev) == ESP_OK && uc8179_init_gray4(dev) == ESP_OK);
    CHECK(uc8179_display_gray4(dev,pixels,sizeof(pixels)) == ESP_OK);
    CHECK(uc8179_sleep(dev) == ESP_OK && uc8179_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}

static int test_gt_constructor_failures(void)
{
    gt911_config_t cfg = touch_config(); gt911_handle_t dev = NULL;
    for (unsigned fail = 1; fail <= 3; ++fail) {
        allocation_calls = 0; allocation_fail_at = fail;
        CHECK(gt911_new(&cfg,&dev) == ESP_ERR_NO_MEM);
        CHECK(!dev && allocation_count == 0 && !invalid_free);
    }
    allocation_fail_at = 0; gt_bad_id = true;
    CHECK(gt911_new(&cfg,&dev) == ESP_ERR_NOT_FOUND && !dev && allocation_count == 0);
    gt_bad_id = false; gt_rx_fail = true;
    CHECK(gt911_new(&cfg,&dev) == ESP_ERR_TIMEOUT && !dev && allocation_count == 0);
    return 0;
}
static int test_gt_geometry(void)
{
    gt911_config_t cfg = touch_config(); gt911_handle_t dev;
    cfg.width = cfg.height = 1;
    CHECK(gt911_new(&cfg,&dev) == ESP_ERR_INVALID_ARG && allocation_count == 0);
    cfg.width = 5000; cfg.height = 800;
    CHECK(gt911_new(&cfg,&dev) == ESP_ERR_INVALID_ARG && allocation_count == 0);
    cfg.width = cfg.height = 0; gt_width = 0;
    CHECK(gt911_new(&cfg,&dev) == ESP_ERR_INVALID_RESPONSE && allocation_count == 0);
    gt_width = 480;
    CHECK(gt911_new(&cfg,&dev) == ESP_OK);
    uint16_t width, height;
    CHECK(gt911_get_native_resolution(dev,&width,&height) == ESP_OK && width == 480 && height == 800);
    CHECK(gt_timeout == 100 && address_latched == 0 && pin_interrupt[9] == GPIO_INTR_DISABLE);
    CHECK(gt911_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}
static int test_gt_no_stale_touch(void)
{
    gt911_config_t cfg = touch_config(); gt911_handle_t dev;
    CHECK(gt911_new(&cfg,&dev) == ESP_OK);
    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS]; uint8_t count = 99;
    gt_ready = true; gt_samples = 1;
    CHECK(gt911_read(dev,points,&count) == ESP_OK && count == 1 && points[0].x == 100);
    CHECK(gt911_read(dev,points,&count) == ESP_OK && count == 0 && gt_reads == 1);
    gt_rx_fail = true;
    CHECK(gt911_read(dev,points,&count) == ESP_ERR_TIMEOUT && count == 0);
    gt_rx_fail = false; gt_ready = true; gt_samples = 0;
    CHECK(gt911_read(dev,points,&count) == ESP_OK && count == 0);
    CHECK(gt911_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}
static int test_gt_sleep_and_mapping(void)
{
    gt911_config_t cfg = touch_config(); gt911_handle_t dev;
    cfg.address = GT911_ADDRESS_HIGH; cfg.mirror_x = true; cfg.swap_xy = true;
    CHECK(gt911_new(&cfg,&dev) == ESP_OK && address_latched == 1);
    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS]; uint8_t count;
    gt_ready = true; gt_samples = 1; gt_x = 0; gt_y = 0;
    CHECK(gt911_read(dev,points,&count) == ESP_OK && count == 1 && points[0].x == 0 && points[0].y == 479);
    int64_t before = clock_us;
    CHECK(gt911_sleep(dev) == ESP_OK && gt_sleep_calls == 1 && clock_us-before >= 58000);
    CHECK(gt911_read(dev,points,&count) == ESP_ERR_INVALID_STATE && count == 0);
    CHECK(gt911_wake(dev) == ESP_OK && pin_mode[9] == GPIO_MODE_INPUT);
    CHECK(gt911_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}
static int test_lis_failures_and_reading(void)
{
    lis2dw12_idf_config_t cfg = lis_config(); lis2dw12_handle_t dev;
    fail_i2c = true;
    CHECK(lis2dw12_idf_new(&cfg,&dev) == ESP_ERR_TIMEOUT && !dev && allocation_count == 0 && i2c_devices == 0);
    fail_i2c = false; registers[LIS2DW12_WHO_AM_I] = 0;
    CHECK(lis2dw12_idf_new(&cfg,&dev) == ESP_ERR_NOT_FOUND && allocation_count == 0 && i2c_devices == 0);
    registers[LIS2DW12_WHO_AM_I] = LIS2DW12_ID;
    CHECK(lis2dw12_idf_new(&cfg,&dev) == ESP_OK);
    CHECK(lis2dw12_idf_start(dev,LIS2DW12_2g,LIS2DW12_XL_ODR_25Hz) == ESP_OK);
    lis2dw12_sample_t sample;
    CHECK(lis2dw12_idf_read(dev,&sample) == ESP_ERR_NOT_FINISHED);
    const uint8_t raw[] = {64,0,128,255,0,64};
    memcpy(registers+LIS2DW12_OUT_X_L,raw,sizeof(raw)); registers[LIS2DW12_STATUS] = 1;
    fail_i2c = true;
    CHECK(lis2dw12_idf_read(dev,&sample) == ESP_ERR_TIMEOUT);
    fail_i2c = false;
    CHECK(lis2dw12_idf_read(dev,&sample) == ESP_OK);
    CHECK(sample.raw[0] == 64 && sample.raw[1] == -128 && sample.raw[2] == 16384);
    CHECK(sample.mg[2] > 995 && sample.mg[2] < 1005);
    CHECK(lis2dw12_idf_power_down(dev) == ESP_OK);
    CHECK(lis2dw12_idf_delete(dev) == ESP_OK && allocation_count == 0 && i2c_devices == 0);
    return 0;
}
static int test_lis_reset_timeout(void)
{
    lis2dw12_idf_config_t cfg = lis_config(); lis2dw12_handle_t dev;
    CHECK(lis2dw12_idf_new(&cfg,&dev) == ESP_OK);
    CHECK(lis2dw12_idf_start(dev,(lis2dw12_fs_t)99,LIS2DW12_XL_ODR_25Hz) == ESP_ERR_INVALID_ARG);
    reset_stuck = true;
    CHECK(lis2dw12_idf_start(dev,LIS2DW12_2g,LIS2DW12_XL_ODR_25Hz) == ESP_ERR_TIMEOUT);
    CHECK(clock_us > 0 && clock_us <= 3000000 && i2c_calls < 110);
    CHECK(lis2dw12_idf_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}

static int test_epd_partial(void)
{
    static uint8_t frame[UC8179_FRAME_BYTES];
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev;
    CHECK(uc8179_new(&cfg,&dev) == ESP_OK);
    CHECK(uc8179_display_partial(dev,frame,sizeof(frame)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_init(dev) == ESP_OK);
    CHECK(uc8179_prepare_partial(dev,frame,sizeof(frame)) == ESP_ERR_INVALID_STATE);
    memset(frame,0x35,sizeof(frame));
    CHECK(uc8179_display_frame(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(uc8179_prepare_partial(dev,frame,sizeof(frame)-1) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_prepare_partial(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(old_size == sizeof(frame) && memcmp(old_frame,frame,sizeof(frame)) == 0);
    CHECK(spi_settings[0xe0][0] == 2 && spi_settings[0xe5][0] == 0x6e);
    CHECK(spi_settings[0x50][0] == 0xa9 && spi_settings[0x50][1] == 7);
    CHECK(uc8179_display_frame(dev,frame,sizeof(frame)) == ESP_ERR_INVALID_STATE);
    unsigned on = spi_command_count[4], off = spi_command_count[2];
    CHECK(uc8179_power_off(dev) == ESP_OK && spi_command_count[2] == off+1);
    memset(frame,0xc9,sizeof(frame));
    CHECK(uc8179_display_partial(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(spi_command_count[4] == on+1 && spi_command_count[0x91] == 1 && spi_command_count[0x92] == 1);
    CHECK(new_size == sizeof(frame) && memcmp(new_frame,frame,sizeof(frame)) == 0);
    const uint8_t window[] = {0,0,3,0x1f,0,0,1,0xdf,1};
    CHECK(memcmp(spi_settings[0x90],window,sizeof(window)) == 0);
    CHECK(spi_command_count[7] == 0); /* No deep sleep between partial frames. */
    /* An active drawing burst must not repeat PON or RESET between frames. */
    unsigned reset_before = spi_command_count[0x00];
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),24,100,16,2) == ESP_OK);
        uc8179_update_stats_t timing;
        CHECK(uc8179_get_update_stats(dev,&timing) == ESP_OK);
        CHECK(timing.power_on_us == 0 && timing.power_off_us == 0 && timing.bytes == 4);
    }
    CHECK(spi_command_count[4] == on+1 && spi_command_count[0x00] == reset_before);
    CHECK(uc8179_power_off(dev) == ESP_OK);
    spi_fail_at = spi_calls+6; /* First pixel chunk after power-on/window. */
    CHECK(uc8179_display_partial(dev,frame,sizeof(frame)) == ESP_ERR_TIMEOUT);
    CHECK(uc8179_display_partial(dev,frame,sizeof(frame)) == ESP_ERR_INVALID_STATE);
    spi_fail_at = 0;
    CHECK(uc8179_power_off(dev) == ESP_OK);
    CHECK(uc8179_init(dev) == ESP_OK && uc8179_display_frame(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(uc8179_prepare_partial(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(uc8179_power_off(dev) == ESP_OK && uc8179_sleep(dev) == ESP_OK);
    CHECK(uc8179_display_partial(dev,frame,sizeof(frame)) == ESP_ERR_INVALID_STATE);
    CHECK(uc8179_delete(dev) == ESP_OK && allocation_count == 0 && spi_devices == 0);
    return 0;
}

static int test_epd_partial_region(void)
{
    static uint8_t frame[UC8179_FRAME_BYTES];
    for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)(i*13+7);
    uc8179_config_t cfg = epd_config(); uc8179_handle_t dev;
    CHECK(uc8179_new(&cfg,&dev) == ESP_OK && uc8179_init(dev) == ESP_OK);
    CHECK(uc8179_display_frame(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(uc8179_prepare_partial(dev,frame,sizeof(frame)) == ESP_OK);
    CHECK(uc8179_power_off(dev) == ESP_OK);
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),9,21,1,1) == ESP_OK);
    const uint8_t small[] = {0,8,0,23,0,21,0,22,1};
    CHECK(memcmp(spi_settings[0x90],small,sizeof(small)) == 0 && new_size == 4);
    CHECK(new_frame[0] == frame[2101] && new_frame[1] == frame[2102]);
    CHECK(new_frame[2] == frame[2201] && new_frame[3] == frame[2202]);
    uc8179_update_stats_t stats;
    CHECK(uc8179_get_update_stats(dev,&stats) == ESP_OK && stats.bytes == 4);
    CHECK(stats.power_on_us >= 1000 && stats.refresh_us >= 300);
    unsigned calls = spi_calls;
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),800,0,1,1) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),799,0,2,1) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),0,479,1,2) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),0,0,0,2) == ESP_ERR_INVALID_ARG);
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame)-1,0,0,8,2) == ESP_ERR_INVALID_ARG);
    CHECK(spi_calls == calls);
    CHECK(uc8179_display_partial_region(dev,frame,sizeof(frame),799,479,1,1) == ESP_OK);
    const uint8_t last[] = {3,0x10,3,0x1f,1,0xde,1,0xdf,1};
    CHECK(memcmp(spi_settings[0x90],last,sizeof(last)) == 0 && new_size == 4);
    CHECK(new_frame[0] == frame[47898] && new_frame[1] == frame[47899]);
    CHECK(new_frame[2] == frame[47998] && new_frame[3] == frame[47999]);
    CHECK(uc8179_sleep(dev) == ESP_OK && uc8179_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}

/* Enumerate producer/consumer scheduling at each ownership boundary. The real
 * application serializes these boundaries with one portMUX; pixel work happens
 * outside that lock and may span many consumer/producer turns. */
static int walk_frame_pipeline(frame_pipeline_t p, unsigned depth)
{
    unsigned display = 0, rendering = 0, ready = 0;
    int active = -1, writer = -1;
    for (unsigned i = 0; i < FRAME_PIPELINE_SLOTS; ++i) {
        CHECK(p.state[i] >= FRAME_FREE && p.state[i] <= FRAME_DISPLAY);
        if (p.state[i] == FRAME_DISPLAY) { ++display; active = (int)i; }
        if (p.state[i] == FRAME_RENDERING) { ++rendering; writer = (int)i; }
        if (p.state[i] == FRAME_READY) ++ready;
    }
    CHECK(display <= 1 && rendering <= 1 && ready <= 1);
    if (!depth) return 0;
    frame_pipeline_t q = p;
    int slot = frame_pipeline_begin_render(&q);
    CHECK(slot != active || slot < 0);
    if (writer >= 0) CHECK(slot < 0 && memcmp(&p,&q,sizeof(p)) == 0);
    else {
        CHECK(slot >= 0 && q.state[slot] == FRAME_RENDERING);
        int line = walk_frame_pipeline(q,depth-1); if (line) return line;
    }
    q = p;
    if (writer >= 0) {
        CHECK(frame_pipeline_publish(&q,writer));
        if (active >= 0) CHECK(q.state[active] == FRAME_DISPLAY);
        int line = walk_frame_pipeline(q,depth-1); if (line) return line;
    }
    q = p;
    slot = frame_pipeline_take_latest(&q);
    if (!ready) CHECK(slot < 0 && memcmp(&p,&q,sizeof(p)) == 0);
    else {
        CHECK(slot >= 0 && p.state[slot] == FRAME_READY && q.state[slot] == FRAME_DISPLAY);
        if (writer >= 0) CHECK(q.state[writer] == FRAME_RENDERING);
        int line = walk_frame_pipeline(q,depth-1); if (line) return line;
    }
    return 0;
}

static uint8_t art_flash[ART_STORE_MIN_BYTES],art_flash_before[ART_STORE_MIN_BYTES];
static unsigned art_write_calls,art_fail_write,art_fail_after;
static bool art_fail_read,art_fail_erase;
static bool art_read(void *ctx,uint32_t offset,void *bytes,size_t n)
{
    (void)ctx;
    if(art_fail_read || offset>sizeof(art_flash) || n>sizeof(art_flash)-offset)return false;
    memcpy(bytes,art_flash+offset,n);return true;
}
static bool art_write(void *ctx,uint32_t offset,const void *bytes,size_t n)
{
    (void)ctx; ++art_write_calls;
    if(offset>sizeof(art_flash) || n>sizeof(art_flash)-offset)return false;
    bool fail=art_write_calls==art_fail_write;
    if(fail && n>art_fail_after)n=art_fail_after;
    const uint8_t *input=bytes;
    for(size_t i=0;i<n;++i){if((art_flash[offset+i]&input[i])!=input[i])return false;art_flash[offset+i]&=input[i];}
    return !fail;
}
static bool art_erase(void *ctx,uint32_t offset,size_t n)
{
    (void)ctx;
    if(art_fail_erase || offset%ART_STORE_SECTOR_BYTES || n%ART_STORE_SECTOR_BYTES ||
       offset>sizeof(art_flash) || n>sizeof(art_flash)-offset)return false;
    memset(art_flash+offset,255,n);return true;
}
static int test_art_frame(void)
{
    static uint8_t packet[ART_FRAME_PACKET_BYTES];
    CHECK(art_crc32((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    for(unsigned i=ART_FRAME_HEADER_BYTES;i<sizeof(packet);++i)packet[i]=0x1b;
    CHECK(art_frame_encode(packet,sizeof(packet),packet+ART_FRAME_HEADER_BYTES,ART_FRAME_PIXEL_BYTES)==ART_FRAME_OK);
    art_frame_view_t view;
    CHECK(art_frame_parse(packet,sizeof(packet),&view)==ART_FRAME_OK && view.pixels==packet+64 && view.pixels[0]==0x1b);
    CHECK(art_frame_parse(packet,sizeof(packet)-1,&view)==ART_FRAME_SIZE && !view.pixels);
    packet[100]^=1;
    CHECK(art_frame_parse(packet,sizeof(packet),&view)==ART_FRAME_PIXEL_CRC && !view.pixels);
    packet[100]^=1;packet[24]^=1;
    CHECK(art_frame_parse(packet,sizeof(packet),&view)==ART_FRAME_HEADER_CRC);
    packet[24]^=1;packet[12]^=1;
    CHECK(art_frame_parse(packet,sizeof(packet),&view)==ART_FRAME_GEOMETRY);
    packet[12]^=1;packet[17]=1;
    CHECK(art_frame_parse(packet,sizeof(packet),&view)==ART_FRAME_FORMAT);
    packet[17]=0;packet[32]=1;
    CHECK(art_frame_parse(packet,sizeof(packet),&view)==ART_FRAME_FORMAT);
    CHECK(art_frame_parse(NULL,sizeof(packet),&view)==ART_FRAME_ARGUMENT);
    return 0;
}
static int test_art_store(void)
{
    static uint8_t old_frame[ART_FRAME_PACKET_BYTES],new_frame[ART_FRAME_PACKET_BYTES],loaded[ART_FRAME_PACKET_BYTES];
    memset(art_flash,255,sizeof(art_flash));art_write_calls=art_fail_write=0;art_fail_read=art_fail_erase=false;
    const art_store_io_t io={.size=sizeof(art_flash),.read=art_read,.write=art_write,.erase=art_erase};
    art_store_t store;
    CHECK(art_store_open(&store,&io)==ART_STORE_EMPTY && store.ready);
    CHECK(art_store_load(&store,loaded,sizeof(loaded))==ART_STORE_EMPTY);
    memset(old_frame+64,0x1b,ART_FRAME_PIXEL_BYTES);memset(new_frame+64,0xe4,ART_FRAME_PIXEL_BYTES);
    CHECK(art_frame_encode(old_frame,sizeof(old_frame),old_frame+64,ART_FRAME_PIXEL_BYTES)==ART_FRAME_OK);
    CHECK(art_frame_encode(new_frame,sizeof(new_frame),new_frame+64,ART_FRAME_PIXEL_BYTES)==ART_FRAME_OK);
    CHECK(art_store_save(&store,old_frame,sizeof(old_frame))==ART_STORE_OK && store.generation==1 && store.active_slot==0);
    memcpy(art_flash_before,art_flash,sizeof(art_flash));
    const unsigned cuts[]={0,1,8,24,32,63,64,96063};
    for(unsigned operation=1;operation<=2;++operation)for(unsigned c=0;c<sizeof(cuts)/sizeof(cuts[0]);++c){
        if(operation==2 && cuts[c]>=64)continue;
        memcpy(art_flash,art_flash_before,sizeof(art_flash));
        CHECK(art_store_open(&store,&io)==ART_STORE_OK);
        art_write_calls=0;art_fail_write=operation;art_fail_after=cuts[c];
        CHECK(art_store_save(&store,new_frame,sizeof(new_frame))==ART_STORE_IO);
        art_fail_write=0;
        CHECK(art_store_open(&store,&io)==ART_STORE_OK && store.generation==1);
        CHECK(art_store_load(&store,loaded,sizeof(loaded))==ART_STORE_OK && !memcmp(loaded,old_frame,sizeof(loaded)));
    }
    CHECK(art_store_save(&store,new_frame,sizeof(new_frame))==ART_STORE_OK && store.generation==2 && store.active_slot==1);
    CHECK(art_store_open(&store,&io)==ART_STORE_OK && store.generation==2);
    CHECK(art_store_load(&store,loaded,sizeof(loaded))==ART_STORE_OK && !memcmp(loaded,new_frame,sizeof(loaded)));
    /* Corruption of the newest picture recovers the earlier complete picture. */
    art_flash[ART_STORE_SLOT_BYTES+ART_STORE_SECTOR_BYTES+64+123]^=1;
    CHECK(art_store_open(&store,&io)==ART_STORE_OK && store.generation==1 && store.active_slot==0);
    art_fail_erase=true;
    CHECK(art_store_save(&store,new_frame,sizeof(new_frame))==ART_STORE_IO);
    art_fail_erase=false;art_fail_read=true;
    CHECK(art_store_open(&store,&io)==ART_STORE_IO && !store.ready);
    CHECK(art_store_save(&store,new_frame,sizeof(new_frame))==ART_STORE_ARGUMENT);
    art_fail_read=false;
    CHECK(art_store_open(&store,&io)==ART_STORE_OK);
    unsigned calls=art_write_calls;new_frame[100]^=1;
    CHECK(art_store_save(&store,new_frame,sizeof(new_frame))==ART_STORE_INVALID_FRAME && art_write_calls==calls);
    return 0;
}

static int test_frame_pipeline(void)
{
    frame_pipeline_t p = {0};
    CHECK(frame_pipeline_begin_render(NULL) == -1 && frame_pipeline_take_latest(NULL) == -1);
    CHECK(!frame_pipeline_publish(NULL,0) && !frame_pipeline_publish(&p,-1));
    CHECK(!frame_pipeline_publish(&p,FRAME_PIPELINE_SLOTS) && !frame_pipeline_publish(&p,0));
    CHECK(frame_pipeline_take_latest(&p) == -1);
    unsigned pixels[FRAME_PIPELINE_SLOTS] = {0};
    int first = frame_pipeline_begin_render(&p);
    CHECK(first >= 0 && frame_pipeline_take_latest(&p) == -1);
    pixels[first] = 11;
    CHECK(frame_pipeline_publish(&p,first));
    CHECK(!frame_pipeline_publish(&p,first));
    CHECK(frame_pipeline_take_latest(&p) == first);
    /* Display stays BUSY on frame 11 while ten newer frames are rendered. */
    for (unsigned value = 12; value <= 21; ++value) {
        int next = frame_pipeline_begin_render(&p);
        CHECK(next >= 0 && next != first && frame_pipeline_begin_render(&p) == -1);
        CHECK(frame_pipeline_take_latest(&p) == -1);
        pixels[next] = value;
        CHECK(frame_pipeline_publish(&p,next) && pixels[first] == 11);
    }
    int latest = frame_pipeline_take_latest(&p);
    CHECK(latest >= 0 && latest != first && pixels[latest] == 21);
    CHECK(p.state[first] == FRAME_FREE);
    CHECK(frame_pipeline_begin_render(&p) == first && pixels[latest] == 21);
    frame_pipeline_t empty = {0};
    return walk_frame_pipeline(empty,12);
}

static int test_cleanup_policy(void)
{
    touch_test_t t = { .contacts = 1, .last_activity_us = 1000000 };
    CHECK(touch_test_refresh_policy(&t,false,0,30,0,1000001) == TOUCH_REFRESH_FULL);
    CHECK(touch_test_refresh_policy(&t,true,29,30,0,1000001) == TOUCH_REFRESH_PARTIAL);
    CHECK(touch_test_refresh_policy(&t,true,30,30,0,9000000) == TOUCH_REFRESH_WAIT);
    t.contacts = 0;
    CHECK(touch_test_refresh_policy(&t,true,30,30,0,2199999) == TOUCH_REFRESH_WAIT);
    CHECK(touch_test_refresh_policy(&t,true,30,30,0,2200000) == TOUCH_REFRESH_FULL);
    CHECK(touch_test_refresh_policy(&t,true,5,5,0,2200000) == TOUCH_REFRESH_FULL);
    CHECK(touch_test_refresh_policy(&t,true,4,5,0,2200000) == TOUCH_REFRESH_PARTIAL);
    t.contacts = 5; t.full_generation = 7;
    CHECK(touch_test_refresh_policy(&t,true,30,30,6,1000001) == TOUCH_REFRESH_FULL);
    CHECK(touch_test_refresh_policy(&t,true,0,30,7,1000001) == TOUCH_REFRESH_PARTIAL);
    touch_test_clear(&t);
    CHECK(t.full_generation == 7 && t.last_activity_us == 1000000);
    CHECK(touch_test_refresh_policy(&t,true,0,30,6,1000001) == TOUCH_REFRESH_FULL);
    return 0;
}

static int test_changed_rectangle(void)
{
    static uint8_t before[DIAG_FRAME_BYTES], after[DIAG_FRAME_BYTES];
    memset(before,0,sizeof(before)); memset(after,0,sizeof(after));
    touch_test_region_t r = {99,99,99,99};
    CHECK(!touch_test_changed_region(before,after,sizeof(before),&r) && r.width == 0);
    after[0] = 0x80;
    CHECK(touch_test_changed_region(before,after,sizeof(before),&r));
    CHECK(r.x == 0 && r.y == 0 && r.width == 16 && r.height == 2);
    after[0] = 0; after[DIAG_FRAME_BYTES-1] = 1;
    CHECK(touch_test_changed_region(before,after,sizeof(before),&r));
    CHECK(r.x == 784 && r.y == 478 && r.width == 16 && r.height == 2);
    after[0] = 1;
    CHECK(touch_test_changed_region(before,after,sizeof(before),&r));
    CHECK(r.x == 0 && r.y == 0 && r.width == 800 && r.height == 480);
    CHECK(!touch_test_changed_region(before,after,sizeof(before)-1,&r) && r.width == 0);
    CHECK(!touch_test_changed_region(NULL,after,sizeof(before),&r));
    return 0;
}

static int test_gt_fresh_release(void)
{
    gt911_config_t cfg = touch_config(); gt911_handle_t dev;
    CHECK(gt911_new(&cfg,&dev) == ESP_OK);
    esp_lcd_touch_point_data_t points[GT911_MAX_POINTS];
    uint8_t count = 99; bool fresh = true;
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_OK && !fresh && count == 0);
    gt_ready = true; gt_samples = 1;
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_OK && fresh && count == 1);
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_OK && !fresh && count == 0);
    gt_ready = true; gt_samples = 0;
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_OK && fresh && count == 0);
    gt_ready = true; gt_samples = 5;
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_OK && fresh && count == 5);
    gt_ready = true; gt_samples = 6;
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_ERR_INVALID_RESPONSE && !fresh && count == 0);
    gt_rx_fail = true; fresh = true;
    CHECK(gt911_read_frame(dev,points,&count,&fresh) == ESP_ERR_TIMEOUT && !fresh && count == 0);
    CHECK(gt911_read_frame(dev,points,&count,NULL) == ESP_ERR_INVALID_ARG && count == 0);
    CHECK(gt911_delete(dev) == ESP_OK && allocation_count == 0);
    return 0;
}

static int test_touch_bench_model(void)
{
    touch_test_t t = {0};
    touch_test_clear(&t);
    touch_test_point_t p[5] = {{50,136,9,1},{200,250,12,2}};
    CHECK(t.screen.touch_x == -1 && t.contacts == 0);
    CHECK(touch_test_accept(&t,p,1,800,480));
    CHECK(t.contacts == 1 && t.strokes == 1 && t.trace_count == 1 && t.coverage == 1);
    CHECK(touch_test_accept(&t,p,1,800,480) && t.trace_count == 1 && t.strokes == 1);
    p[0].x = 70;
    CHECK(touch_test_accept(&t,p,1,800,480) && t.trace_count == 2);
    CHECK(touch_test_accept(&t,NULL,0,800,480) && t.contacts == 0);
    CHECK(touch_test_accept(&t,p,1,800,480) && t.strokes == 2 && t.trace_count == 3);
    CHECK(t.trace[1].stroke != t.trace[2].stroke);
    CHECK(touch_test_accept(&t,p,2,800,480) && t.max_contacts == 2);
    CHECK(touch_test_accept(&t,p,1,800,480) && t.trace_count == 4);
    CHECK(t.trace[2].stroke != t.trace[3].stroke);
    uint32_t samples = t.screen.touch_samples;
    p[1].x = 800;
    CHECK(!touch_test_accept(&t,p,2,800,480) && t.screen.touch_samples == samples);
    CHECK(!touch_test_accept(&t,p,6,800,480));
    CHECK(!touch_test_accept(&t,NULL,1,800,480));
    for (unsigned row = 0; row < 3; ++row) for (unsigned col = 0; col < 8; ++col) {
        p[0].x = (uint16_t)(col*100+50); p[0].y = (uint16_t)(80+row*112+56);
        CHECK(touch_test_accept(&t,p,1,800,480));
    }
    CHECK(t.coverage == TOUCH_TEST_ALL_CELLS);
    for (unsigned i = 0; i < 300; ++i) {
        p[0].x = (uint16_t)((i*7)%800);
        CHECK(touch_test_accept(&t,p,1,800,480));
    }
    CHECK(t.trace_count == TOUCH_TEST_TRACE && t.trace_next < TOUCH_TEST_TRACE);
    CHECK(touch_test_button(20,438) == 'r' && touch_test_button(259,479) == 'r');
    CHECK(touch_test_button(280,450) == 'c' && touch_test_button(779,479) == 'n');
    CHECK(!touch_test_button(800,450) && !touch_test_button(20,437));
    CHECK(!touch_test_button(260,450) && !touch_test_button(-1,479));
    CHECK(touch_test_next_ui_page(DIAG_GRID) == DIAG_STATUS);
    CHECK(touch_test_next_ui_page(DIAG_STATUS) == DIAG_GRID);
    CHECK(!touch_test_controls_ready(DIAG_GRID,DIAG_WHITE));
    CHECK(!touch_test_controls_ready(UINT32_MAX,DIAG_GRID));
    CHECK(touch_test_controls_ready(DIAG_GRID,DIAG_GRID));
    touch_test_clear(&t);
    CHECK(t.trace_count == 0 && t.coverage == 0 && t.strokes == 0 && t.max_contacts == 0);
    return 0;
}

static const struct { const char *name; int (*run)(void); } cases[] = {
    {"UC8179 allocation / attach failures",test_epd_alloc_failures},
    {"UC8179 stuck BUSY timeout",test_epd_busy_timeout},
    {"UC8179 frame chunks, transfer recovery, sleep, cleanup",test_epd_frame_and_recovery},
    {"UC8179 OTP partial, OLD baseline, full window, POF/wake and recovery",test_epd_partial},
    {"UC8179 cropped partial stride, byte alignment, last pixel and timing",test_epd_partial_region},
    {"UC8179 gray4 OTP, all pixel codes, exact bitplanes, bounds and mode transitions",test_epd_gray4_planes},
    {"UC8179 gray4 power, transfer and refresh BUSY faults / recovery",test_epd_gray4_recovery},
    {"Changed rectangle: no-op, single byte, last row/column and full frame",test_changed_rectangle},
    {"Cleanup: partial budget, release/idle boundary, manual request and clear",test_cleanup_policy},
    {"Frame pipeline: immutable active frame, latest replacement and interleaved ownership",test_frame_pipeline},
    {"Art frame: 4-level packet, CRC, geometry, reserved fields and truncation",test_art_frame},
    {"Art store: replace, restart, torn writes, CRC recovery and failed-open write protection",test_art_store},
    {"GT911 allocation, absent device and wrong ID",test_gt_constructor_failures},
    {"GT911 explicit/automatic geometry validation",test_gt_geometry},
    {"GT911 fresh frame vs cached touch / release / I2C fault",test_gt_no_stale_touch},
    {"GT911 fresh/no-report/release distinction and malformed report",test_gt_fresh_release},
    {"Touch bench strokes, multitouch, coverage, ring and bounds",test_touch_bench_model},
    {"GT911 address, mirrored coordinates and wake pin direction",test_gt_sleep_and_mapping},
    {"LIS2DW12 I2C faults, WHO_AM_I, real ST register API and mg",test_lis_failures_and_reading},
    {"LIS2DW12 bounded reset and invalid configuration",test_lis_reset_timeout},
};
int host_case_count(void) { return (int)(sizeof(cases)/sizeof(cases[0])); }
const char *host_case_name(int i) { return i >= 0 && i < host_case_count() ? cases[i].name : "invalid"; }
const char *host_failure(void) { return failure ? failure : ""; }
int host_case_run(int index)
{
    memset(spi_settings,0,sizeof(spi_settings));
    memset(spi_command_count,0,sizeof(spi_command_count));
    if (index < 0 || index >= host_case_count()) return -1;
    memset(allocated,0,sizeof(allocated)); allocation_calls = allocation_fail_at = allocation_count = 0;
    invalid_free = false; failure = NULL; clock_us = 0; busy_level = 1; busy_after_refresh = false;
    memset(pin_level,0,sizeof(pin_level)); memset(pin_mode,0,sizeof(pin_mode)); memset(pin_interrupt,0,sizeof(pin_interrupt));
    address_latched = 0; fail_spi_add = fail_spi_remove = spi_driver_ready = false;
    spi_calls = spi_fail_at = spi_devices = 0; bus_max = 4096; largest_chunk = old_size = new_size = 0;
    i2c_devices = i2c_calls = 0; fail_i2c = reset_stuck = false; memset(registers,0,sizeof(registers));
    registers[LIS2DW12_WHO_AM_I] = LIS2DW12_ID;
    gt_width = 480; gt_height = 800; gt_x = 100; gt_y = 200;
    gt_bad_id = gt_rx_fail = gt_ready = false; gt_samples = gt_cached = 0; gt_reads = gt_sleep_calls = 0;
    return cases[index].run();
}
