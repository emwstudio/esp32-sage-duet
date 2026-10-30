#include "classic_shaolan_audio.hpp"
#include "ima_adpcm.hpp"
#include <cstring>

namespace {
struct EmbeddedAudio {
    const uint8_t *start;
    const uint8_t *end;
};

#define AUDIO_SYMBOLS(index) \
    extern const uint8_t audio_##index##_start[] asm("_binary_shaolan_" #index "_wav_start"); \
    extern const uint8_t audio_##index##_end[] asm("_binary_shaolan_" #index "_wav_end")

AUDIO_SYMBOLS(00);
AUDIO_SYMBOLS(01);
AUDIO_SYMBOLS(02);
AUDIO_SYMBOLS(03);
AUDIO_SYMBOLS(04);
AUDIO_SYMBOLS(05);
AUDIO_SYMBOLS(06);
AUDIO_SYMBOLS(07);
AUDIO_SYMBOLS(08);
AUDIO_SYMBOLS(09);

const EmbeddedAudio clips[] = {
    {audio_00_start, audio_00_end}, {audio_01_start, audio_01_end},
    {audio_02_start, audio_02_end}, {audio_03_start, audio_03_end},
    {audio_04_start, audio_04_end}, {audio_05_start, audio_05_end},
    {audio_06_start, audio_06_end}, {audio_07_start, audio_07_end},
    {audio_08_start, audio_08_end}, {audio_09_start, audio_09_end},
};

void write16(uint8_t *data, uint16_t value)
{
    data[0] = value & 0xff;
    data[1] = value >> 8;
}

void write32(uint8_t *data, uint32_t value)
{
    write16(data, value & 0xffff);
    write16(data + 2, value >> 16);
}
}

size_t classic_shaolan_audio(uint32_t turn, uint8_t *wav, size_t capacity)
{
    if (wav == nullptr || turn == 0 || capacity < 44) return 0;
    const auto &clip = clips[(turn / 2) % (sizeof(clips) / sizeof(clips[0]))];
    size_t pcm_size = decode_ima_adpcm_wav(clip.start, clip.end - clip.start, wav + 44, capacity - 44);
    if (pcm_size == 0 || pcm_size % 2 != 0 || pcm_size * 2 > capacity - 44) return 0;
    auto *samples = reinterpret_cast<int16_t *>(wav + 44);
    for (size_t index = pcm_size / 2; index-- > 0;) {
        samples[index * 2] = samples[index];
        samples[index * 2 + 1] = samples[index];
    }
    pcm_size *= 2;
    std::memcpy(wav, "RIFF", 4);
    write32(wav + 4, 36 + pcm_size);
    std::memcpy(wav + 8, "WAVEfmt ", 8);
    write32(wav + 16, 16);
    write16(wav + 20, 1);
    write16(wav + 22, 2);
    write32(wav + 24, 24000);
    write32(wav + 28, 24000 * 4);
    write16(wav + 32, 4);
    write16(wav + 34, 16);
    std::memcpy(wav + 36, "data", 4);
    write32(wav + 40, pcm_size);
    return pcm_size + 44;
}
