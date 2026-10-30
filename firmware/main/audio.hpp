#pragma once
#include "app_state.hpp"
#include "button.hpp"
#include <atomic>
#include <functional>

class Audio {
public:
    bool init();
    size_t record(uint8_t *buffer, size_t capacity, Button &button,
                  const std::function<void(int)> &progress, bool &cancelled);
    bool play(const uint8_t *wav, size_t size, Button &button,
              std::atomic<bool> &cancel, const std::function<void(int)> &frame);
    void silence();

private:
    bool ready_ = false;
    bool playback_configured_ = false;
};
