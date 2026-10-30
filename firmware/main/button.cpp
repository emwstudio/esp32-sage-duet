#include "button.hpp"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace {
constexpr uint32_t PWR_PIN_MASK = IO_EXPANDER_PIN_NUM_4;
esp_io_expander_handle_t expander = nullptr;

bool update(int raw, int &candidate, int &stable, int &count, bool &armed)
{
    if (raw != candidate) {
        candidate = raw;
        count = 1;
        return false;
    }
    if (count < 3) {
        ++count;
        return false;
    }
    if (raw != stable) {
        stable = raw;
        if (stable == 1) {
            armed = true;
        } else if (armed) {
            armed = false;
            return true;
        }
    }
    return false;
}
}

void Button::init()
{
    const gpio_config_t boot_config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&boot_config) != ESP_OK) {
        ESP_LOGE("Button", "BOOT input config failed");
    }
    if (bsp_io_expander_try_init(&expander) != ESP_OK) {
        ESP_LOGE("Button", "PWR expander init failed");
        return;
    }
    if (esp_io_expander_set_dir(expander, PWR_PIN_MASK, IO_EXPANDER_INPUT) != ESP_OK) {
        ESP_LOGE("Button", "PWR input config failed");
    }
}

bool Button::clicked()
{
    bool pwr_clicked = false;
    uint32_t levels = 0;
    if (expander != nullptr && esp_io_expander_get_level(expander, PWR_PIN_MASK, &levels) == ESP_OK) {
        const int raw = (levels & PWR_PIN_MASK) != 0 ? 1 : 0;
        pwr_clicked = update(raw, candidate_, stable_, count_, armed_);
    }
    const bool boot_clicked = update(gpio_get_level(GPIO_NUM_0) == 0 ? 1 : 0,
        boot_candidate_, boot_stable_, boot_count_, boot_armed_);
    return pwr_clicked || boot_clicked;
}

bool Button::is_pressed() const
{
    uint32_t levels = 0;
    const bool pwr = expander != nullptr && esp_io_expander_get_level(expander, PWR_PIN_MASK, &levels) == ESP_OK &&
        (levels & PWR_PIN_MASK) != 0;
    return pwr || gpio_get_level(GPIO_NUM_0) == 0;
}
