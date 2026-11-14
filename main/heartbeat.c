#include "heartbeat.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static esp_timer_handle_t heartbeat_timer;

static void blink_led(void *arg)
{
    (void)arg;
    static bool on = true;
    on = !on;
    ESP_ERROR_CHECK(gpio_set_level(BOARD_PIN_STATUS_LED, on));
}

esp_err_t heartbeat_start(void)
{
    if (heartbeat_timer) return ESP_ERR_INVALID_STATE;
    esp_err_t err = gpio_set_level(BOARD_PIN_STATUS_LED, 1);
    const gpio_config_t led = {
        .pin_bit_mask = UINT64_C(1) << BOARD_PIN_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (err == ESP_OK) err = gpio_config(&led);
    if (err != ESP_OK) return err;
    const esp_timer_create_args_t timer = {
        .callback = blink_led,
        .name = "led_heartbeat",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&timer, &heartbeat_timer);
    if (err != ESP_OK) return err;
    err = esp_timer_start_periodic(heartbeat_timer, 500000);
    if (err != ESP_OK) {
        esp_timer_delete(heartbeat_timer);
        heartbeat_timer = NULL;
    }
    return err;
}
