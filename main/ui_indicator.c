#include "ui_indicator.h"
#include "proto_bridge.h"
#include "ble_gatt_server.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdatomic.h>

static const char *TAG = "ui";
static led_strip_handle_t strip;
static atomic_uint activity_until;
static atomic_uint clear_until;
static tb_core_t snapshot;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool before(uint32_t now, uint32_t deadline) { return (int32_t)(deadline - now) > 0; }

void ui_indicator_activity(void)
{
#if CONFIG_TB_LED_ACTIVITY
    atomic_store(&activity_until, now_ms() + 50);
#endif
}

void ui_indicator_bonds_cleared(void) { atomic_store(&clear_until, now_ms() + 600); }

static void ui_task(void *arg)
{
    int raw_previous = 1, stable = 1;
    uint32_t changed = now_ms(), pressed = 0, last_status = now_ms();
    bool long_handled = false;
    uint8_t old_red = 255, old_green = 255, old_blue = 255;
    for (;;) {
        uint32_t now = now_ms();
        int raw = gpio_get_level(GPIO_NUM_0);
        if (raw != raw_previous) { changed = now; raw_previous = raw; }
        if (raw != stable && (uint32_t)(now - changed) >= 30) {
            stable = raw;
            if (!stable) { pressed = now; long_handled = false; }
            else if (!long_handled) bridge_print_status();
        }
        if (!stable && !long_handled && (uint32_t)(now - pressed) >= 3000) {
            long_handled = true;
            ESP_LOGI(TAG, "BOOT held 3 seconds: clearing BLE bonds");
            ble_gatt_server_clear_bonds();
        }
        if ((uint32_t)(now - last_status) >= 30000) {
            bridge_print_status();
            ESP_LOGI(TAG, "UI stack watermark=%u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
            last_status = now;
        }
        bridge_status_snapshot(&snapshot);
        uint8_t red = 0, green = 0, blue = 0;
        uint8_t brightness = CONFIG_TB_LED_BRIGHTNESS;
        if (before(now, atomic_load(&clear_until))) {
            if ((now / 100) % 2 == 0) red = blue = brightness;
        } else if (!snapshot.usb_ready) {
            if ((now % 1000) < 500) red = brightness;
        } else if (!snapshot.ble_connected) {
            unsigned phase = now % 500;
            unsigned triangle = phase < 250 ? phase : 500 - phase;
            blue = 1 + (brightness - 1) * triangle / 250;
        } else if (before(now, atomic_load(&activity_until))) {
            green = blue = brightness;
        } else green = brightness;
        if (strip && (red != old_red || green != old_green || blue != old_blue)) {
            esp_err_t err = led_strip_set_pixel(strip, 0, red, green, blue);
            if (err == ESP_OK) err = led_strip_refresh(strip);
            if (err != ESP_OK) ESP_LOGW(TAG, "RGB update: %s", esp_err_to_name(err));
            old_red = red; old_green = green; old_blue = blue;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t ui_indicator_init(void)
{
    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&button);
    if (err != ESP_OK) return err;
#if !CONFIG_TB_LED_DISABLED
#if CONFIG_TB_LED_GPIO48
    const int pin = 48;
#else
    const int pin = 38;
#endif
    const led_strip_config_t config = {
        .strip_gpio_num = pin, .max_leds = 1, .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {.resolution_hz = 10000000, .flags.with_dma = true};
    err = led_strip_new_rmt_device(&config, &rmt, &strip);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "RGB LED GPIO%d; BOOT GPIO0", pin);
#endif
    return xTaskCreatePinnedToCore(ui_task, "ui_task", 3072, NULL, 5, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
