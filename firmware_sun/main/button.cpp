#include "../../firmware/main/button.hpp"
#include "driver/gpio.h"

void Button::init()
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_18,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
}

bool Button::clicked()
{
    const int raw = gpio_get_level(GPIO_NUM_18);
    if (raw != candidate_) {
        candidate_ = raw;
        count_ = 1;
        return false;
    }
    if (count_ < 3) {
        ++count_;
        return false;
    }
    if (raw != stable_) {
        stable_ = raw;
        if (stable_ == 0) {
            armed_ = true;
        } else if (armed_) {
            armed_ = false;
            return true;
        }
    }
    return false;
}

bool Button::is_pressed() const
{
    return gpio_get_level(GPIO_NUM_18) == 0;
}
