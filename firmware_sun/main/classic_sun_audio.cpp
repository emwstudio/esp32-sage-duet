#include "classic_sun_audio.hpp"

namespace {
struct EmbeddedAudio {
    const uint8_t *start;
    const uint8_t *end;
};

#define AUDIO_SYMBOLS(index) \
    extern const uint8_t audio_##index##_start[] asm("_binary_" #index "_wav_start"); \
    extern const uint8_t audio_##index##_end[] asm("_binary_" #index "_wav_end")

AUDIO_SYMBOLS(0);
AUDIO_SYMBOLS(1);
AUDIO_SYMBOLS(2);
AUDIO_SYMBOLS(3);
AUDIO_SYMBOLS(4);
AUDIO_SYMBOLS(5);
AUDIO_SYMBOLS(6);
AUDIO_SYMBOLS(7);
AUDIO_SYMBOLS(8);
AUDIO_SYMBOLS(9);
AUDIO_SYMBOLS(10);
AUDIO_SYMBOLS(11);
AUDIO_SYMBOLS(12);
AUDIO_SYMBOLS(13);
AUDIO_SYMBOLS(14);
AUDIO_SYMBOLS(15);
AUDIO_SYMBOLS(16);
AUDIO_SYMBOLS(17);
AUDIO_SYMBOLS(18);
AUDIO_SYMBOLS(19);
AUDIO_SYMBOLS(20);
AUDIO_SYMBOLS(21);
AUDIO_SYMBOLS(22);

const EmbeddedAudio clips[] = {
    {audio_0_start, audio_0_end},
    {audio_1_start, audio_1_end},
    {audio_2_start, audio_2_end},
    {audio_3_start, audio_3_end},
    {audio_4_start, audio_4_end},
    {audio_5_start, audio_5_end},
    {audio_6_start, audio_6_end},
    {audio_7_start, audio_7_end},
    {audio_8_start, audio_8_end},
    {audio_9_start, audio_9_end},
    {audio_10_start, audio_10_end},
    {audio_11_start, audio_11_end},
    {audio_12_start, audio_12_end},
    {audio_13_start, audio_13_end},
    {audio_14_start, audio_14_end},
    {audio_15_start, audio_15_end},
    {audio_16_start, audio_16_end},
    {audio_17_start, audio_17_end},
    {audio_18_start, audio_18_end},
    {audio_19_start, audio_19_end},
    {audio_20_start, audio_20_end},
    {audio_21_start, audio_21_end},
    {audio_22_start, audio_22_end},
};
}

size_t classic_sun_audio(uint32_t turn, const uint8_t **data)
{
    if (data == nullptr || turn < 2) return 0;
    const auto &clip = clips[(turn / 2 - 1) % (sizeof(clips) / sizeof(clips[0]))];
    *data = clip.start;
    return clip.end - clip.start;
}
