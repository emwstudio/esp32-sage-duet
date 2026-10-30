#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t AUDIO_SLOT_BYTES = 2 * 1024 * 1024;
constexpr size_t RECORDING_BYTES = 20 * 24000 * 2 * 2 + 44;
constexpr size_t JSON_RESPONSE_BYTES = 16 * 1024;

enum class AppState {
    Idle,
    Recording,
    Creating,
    Downloading,
    Playing,
    WaitingNext,
    Error,
};

struct Turn {
    int number = 0;
    char speaker[16] = {};
    char text[512] = {};
    char audio_url[256] = {};
    int duration_ms = 0;
};
