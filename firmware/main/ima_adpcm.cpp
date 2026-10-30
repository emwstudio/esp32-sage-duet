#include "ima_adpcm.hpp"
#include <algorithm>
#include <cstring>

namespace {
uint16_t read16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8;
}

uint32_t read32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
        static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

void write16(uint8_t *data, int16_t value)
{
    data[0] = value & 0xff;
    data[1] = (value >> 8) & 0xff;
}

constexpr int index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};
constexpr int step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
}

size_t decode_ima_adpcm_wav(const uint8_t *wav, size_t size, uint8_t *pcm, size_t capacity)
{
    if (wav == nullptr || pcm == nullptr || size < 12 || std::memcmp(wav, "RIFF", 4) != 0 ||
        std::memcmp(wav + 8, "WAVE", 4) != 0) return 0;
    size_t offset = 12;
    uint16_t block_align = 0;
    bool valid_format = false;
    while (offset + 8 <= size) {
        const uint32_t chunk_size = read32(wav + offset + 4);
        if (chunk_size > size - offset - 8) return 0;
        const uint8_t *chunk = wav + offset + 8;
        if (std::memcmp(wav + offset, "fmt ", 4) == 0 && chunk_size >= 20) {
            valid_format = read16(chunk) == 0x11 && read16(chunk + 2) == 1 &&
                read32(chunk + 4) == 24000;
            block_align = read16(chunk + 12);
        } else if (std::memcmp(wav + offset, "data", 4) == 0) {
            if (!valid_format || block_align <= 4) return 0;
            size_t output = 0;
            size_t position = 0;
            while (position < chunk_size) {
                const size_t block_size = std::min<size_t>(block_align, chunk_size - position);
                if (block_size <= 4) return 0;
                const uint8_t *block = chunk + position;
                int predictor = static_cast<int16_t>(read16(block));
                int step_index = block[2];
                if (step_index > 88 || output + 2 > capacity) return 0;
                write16(pcm + output, static_cast<int16_t>(predictor));
                output += 2;
                for (size_t index = 4; index < block_size; ++index) {
                    for (int nibble_index = 0; nibble_index < 2; ++nibble_index) {
                        const int nibble = nibble_index == 0 ? block[index] & 0x0f : block[index] >> 4;
                        const int step = step_table[step_index];
                        int difference = step >> 3;
                        if (nibble & 4) difference += step;
                        if (nibble & 2) difference += step >> 1;
                        if (nibble & 1) difference += step >> 2;
                        predictor += (nibble & 8) ? -difference : difference;
                        predictor = std::clamp(predictor, -32768, 32767);
                        step_index = std::clamp(step_index + index_table[nibble], 0, 88);
                        if (output + 2 > capacity) return 0;
                        write16(pcm + output, static_cast<int16_t>(predictor));
                        output += 2;
                    }
                }
                position += block_size;
            }
            return output;
        }
        offset += 8 + chunk_size + (chunk_size & 1);
    }
    return 0;
}
