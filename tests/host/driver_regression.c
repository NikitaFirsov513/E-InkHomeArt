#include "test_sdk.h"
#include <stdlib.h>
#include <string.h>
#include "uc8179.h"
#include "gt911.h"
#include "lis2dw12_idf.h"

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
static unsigned spi_calls, spi_fail_at, spi_devices;
static size_t bus_max, largest_chunk, old_size, new_size;
static uint8_t spi_command, old_frame[UC8179_FRAME_BYTES], new_frame[UC8179_FRAME_BYTES];
esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t host, size_t *length)
{ (void)host; *length = bus_max; return ESP_OK; }
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *config, spi_device_handle_t *out)
{
    (void)host;
    if (fail_spi_add) return ESP_ERR_NO_MEM;
    if (config->mode || config->spics_io_num != 47) return ESP_ERR_INVALID_ARG;
    *out = (void *)(uintptr_t)0x123; ++spi_devices; return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t device)
{ (void)device; if (fail_spi_remove) return ESP_ERR_INVALID_STATE; --spi_devices; return ESP_OK; }
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *tx)
{
    (void)device;
    if (++spi_calls == spi_fail_at) return ESP_ERR_TIMEOUT;
    if (tx->flags & SPI_TRANS_USE_TXDATA) {
        spi_command = tx->tx_data[0];
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
    }
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

static const struct { const char *name; int (*run)(void); } cases[] = {
    {"UC8179 allocation / attach failures",test_epd_alloc_failures},
    {"UC8179 stuck BUSY timeout",test_epd_busy_timeout},
    {"UC8179 frame chunks, transfer recovery, sleep, cleanup",test_epd_frame_and_recovery},
    {"GT911 allocation, absent device and wrong ID",test_gt_constructor_failures},
    {"GT911 explicit/automatic geometry validation",test_gt_geometry},
    {"GT911 fresh frame vs cached touch / release / I2C fault",test_gt_no_stale_touch},
    {"GT911 address, mirrored coordinates and wake pin direction",test_gt_sleep_and_mapping},
    {"LIS2DW12 I2C faults, WHO_AM_I, real ST register API and mg",test_lis_failures_and_reading},
    {"LIS2DW12 bounded reset and invalid configuration",test_lis_reset_timeout},
};
int host_case_count(void) { return (int)(sizeof(cases)/sizeof(cases[0])); }
const char *host_case_name(int i) { return i >= 0 && i < host_case_count() ? cases[i].name : "invalid"; }
const char *host_failure(void) { return failure ? failure : ""; }
int host_case_run(int index)
{
    if (index < 0 || index >= host_case_count()) return -1;
    memset(allocated,0,sizeof(allocated)); allocation_calls = allocation_fail_at = allocation_count = 0;
    invalid_free = false; failure = NULL; clock_us = 0; busy_level = 1;
    memset(pin_level,0,sizeof(pin_level)); memset(pin_mode,0,sizeof(pin_mode)); memset(pin_interrupt,0,sizeof(pin_interrupt));
    address_latched = 0; fail_spi_add = fail_spi_remove = false;
    spi_calls = spi_fail_at = spi_devices = 0; bus_max = 4096; largest_chunk = old_size = new_size = 0;
    i2c_devices = i2c_calls = 0; fail_i2c = reset_stuck = false; memset(registers,0,sizeof(registers));
    registers[LIS2DW12_WHO_AM_I] = LIS2DW12_ID;
    gt_width = 480; gt_height = 800; gt_x = 100; gt_y = 200;
    gt_bad_id = gt_rx_fail = gt_ready = false; gt_samples = gt_cached = 0; gt_reads = gt_sleep_calls = 0;
    return cases[index].run();
}
