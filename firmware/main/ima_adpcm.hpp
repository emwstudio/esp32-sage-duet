#pragma once
#include <cstddef>
#include <cstdint>

size_t decode_ima_adpcm_wav(const uint8_t *wav, size_t size, uint8_t *pcm, size_t capacity);
