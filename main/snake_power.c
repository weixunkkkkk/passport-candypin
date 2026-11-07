#include "snake_power.h"
#include "snake_net.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"

static bool screen_off;
static uint32_t off_at;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void check_shutdown(const char *step, esp_err_t result) {
    if (result != ESP_OK) ESP_LOGW("snake-power", "%s failed: %s", step, esp_err_to_name(result));
}

void snake_power_init(void) {
    const esp_pm_config_t config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
    check_shutdown("CPU power management", esp_pm_configure(&config));
    esp_err_t result = bsp_audio_init();
    if (result == ESP_OK) result = bsp_audio_sleep();
    check_shutdown("Idle audio suspend", result);
}

void snake_power_screen_off(bool off) {
    if (off != __atomic_load_n(&screen_off, __ATOMIC_RELAXED)) {
        if (off) __atomic_store_n(&off_at, now_ms(), __ATOMIC_RELAXED);
        __atomic_store_n(&screen_off, off, __ATOMIC_RELAXED);
    }
    snake_net_screen_off(off);
}

void snake_power_poll(void) {
    snake_net_view_t net;
    if (!snake_net_view(&net)) return;
    if (!snake_power_deep_due(__atomic_load_n(&screen_off, __ATOMIC_RELAXED),
            now_ms() - __atomic_load_n(&off_at, __ATOMIC_RELAXED),
            net.prefs.standby == STANDBY_SAVE, net.suspended, usb_serial_jtag_is_connected())) return;
    int mv = bsp_button_read_mv();
    if (mv < BSP_BTN_MV_RELEASED_MIN || net.setup || !bsp_lvgl_lock(100)) return;
    if (!__atomic_load_n(&screen_off, __ATOMIC_RELAXED) || usb_serial_jtag_is_connected() ||
        !snake_net_view(&net) || net.prefs.standby != STANDBY_SAVE || !net.suspended) {
        bsp_lvgl_unlock();
        return;
    }
    esp_err_t result = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (result != ESP_OK) {
        check_shutdown("Key wake", result);
        bsp_lvgl_unlock();
        return;
    }
    int released = 0;
    if (bsp_button_prepare_deep_sleep(&released) != ESP_OK || released != 1) {
        ESP_LOGW("snake-power", "Key changed during sleep entry; restarting peripherals");
        esp_restart();
        return;
    }
    check_shutdown("Battery suspend", bsp_battery_sleep());
    check_shutdown("Audio suspend", bsp_audio_sleep());
    check_shutdown("I2S release", bsp_audio_prepare_deep_sleep());
    check_shutdown("I2C release", bsp_i2c_prepare_deep_sleep());
    check_shutdown("LCD suspend", bsp_display_prepare_deep_sleep());
    ESP_LOGI("snake-power", "Deep standby; press any key to wake");
    esp_deep_sleep_start();
    esp_restart();
}
