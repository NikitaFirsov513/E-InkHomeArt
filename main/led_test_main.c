#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "heartbeat.h"

/* Reject a cached diagnostics configuration for this minimal assembly test. */
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
#error "LED test requires PSRAM disabled; use a fresh LED-test sdkconfig."
#endif
#if !CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "LED test requires the default UART0 console on TX43/RX44."
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(heartbeat_start());
    ESP_LOGI("LED_TEST", "LED TEST READY: D6 BLINK, 500 ms on / 500 ms off");
    /* Returning deletes the main task; the periodic timer keeps running. */
}
