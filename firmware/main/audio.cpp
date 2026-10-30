#include "audio.hpp"
#include "bsp_board_extra.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <algorithm>

namespace {
struct __attribute__((packed)) WavHeader {
    char riff[4];
    uint32_t riff_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
    char data[4];
    uint32_t data_size;
};
static_assert(sizeof(WavHeader) == 44);

WavHeader make_header(uint32_t size)
{
    return {
        {'R','I','F','F'}, size + 36, {'W','A','V','E'}, {'f','m','t',' '},
        16, 1, 2, 24000, 24000 * 4, 4, 16, {'d','a','t','a'}, size,
    };
}

#if !CONFIG_SAGE_ROLE_SUN
struct Playback {
    const uint8_t *pcm;
    size_t size;
    std::atomic<bool> *cancel;
    uint16_t channels;
    std::atomic<bool> done{false};
    bool success = true;
};

void playback_worker(void *argument)
{
    auto *playback = static_cast<Playback *>(argument);
    int16_t stereo[1024];
    size_t written_total = 0;
    while (written_total < playback->size) {
        if (playback->cancel->load()) {
            playback->success = false;
            break;
        }
        const size_t chunk = std::min<size_t>(playback->channels == 1 ? 2048 : 4096,
                                              playback->size - written_total);
        const uint8_t *source = playback->pcm + written_total;
        size_t output_size = chunk;
        if (playback->channels == 1) {
            const auto *mono = reinterpret_cast<const int16_t *>(source);
            for (size_t index = 0; index < chunk / 2; ++index) {
                stereo[index * 2] = mono[index];
                stereo[index * 2 + 1] = mono[index];
            }
            source = reinterpret_cast<const uint8_t *>(stereo);
            output_size *= 2;
        }
        size_t written = 0;
        const esp_err_t result = bsp_extra_i2s_write(
            const_cast<uint8_t *>(source), output_size, &written, portMAX_DELAY);
        if (result != ESP_OK || written != output_size) {
            ESP_LOGW("Audio", "write failed: %s, %u/%u", esp_err_to_name(result),
                     static_cast<unsigned>(written), static_cast<unsigned>(output_size));
            playback->success = false;
            break;
        }
        written_total += chunk;
    }
    if (playback->success) vTaskDelay(pdMS_TO_TICKS(50));
    playback->done.store(true);
    vTaskDelete(nullptr);
}
#endif
}

bool Audio::init()
{
    ready_ = bsp_extra_codec_init() == ESP_OK;
    if (ready_) {
        (void)bsp_extra_codec_volume_set(100, nullptr);
        playback_configured_ = bsp_extra_codec_set_fs(24000, 16, I2S_SLOT_MODE_STEREO) == ESP_OK;
        ready_ = playback_configured_;
    }
    return ready_;
}

size_t Audio::record(uint8_t *buffer, size_t capacity, Button &button,
                     const std::function<void(int)> &progress, bool &cancelled)
{
    cancelled = false;
    if (!ready_ || buffer == nullptr || capacity < sizeof(WavHeader) + 4096) {
        return 0;
    }
    if (bsp_extra_codec_set_voice_fs(24000, 16, 4,
            BSP_EXTRA_ES7210_TDM_ALL_SLOTS_MASK,
            BSP_EXTRA_ES7210_PHYSICAL_FRONT_MIC_MASK) != ESP_OK) {
        return 0;
    }
    playback_configured_ = false;
    (void)bsp_extra_codec_mute_set(false);
    constexpr size_t frames = 256;
    int16_t raw[frames * 4];
    int16_t stereo[frames * 2];
    size_t bytes = 0;
    int last_second = -1;
    while (bytes + sizeof(stereo) <= capacity - sizeof(WavHeader)) {
        if (button.clicked()) {
            break;
        }
        size_t read = 0;
        if (bsp_extra_i2s_read(raw, sizeof(raw), &read, portMAX_DELAY) != ESP_OK || read != sizeof(raw)) {
            return 0;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            stereo[frame * 2] = raw[frame * 4];
            stereo[frame * 2 + 1] = raw[frame * 4 + 2];
        }
        std::memcpy(buffer + sizeof(WavHeader) + bytes, stereo, sizeof(stereo));
        bytes += sizeof(stereo);
        const int second = static_cast<int>(bytes / (24000 * 4));
        if (second != last_second) {
            last_second = second;
            progress(second);
        }
    }
    WavHeader header = make_header(bytes);
    std::memcpy(buffer, &header, sizeof(header));
    return bytes + sizeof(header);
}

bool Audio::play(const uint8_t *wav, size_t size, Button &button,
                 std::atomic<bool> &cancel, const std::function<void(int)> &frame)
{
    ESP_LOGI("Audio", "play begin: %u bytes", static_cast<unsigned>(size));
    if (!ready_ || wav == nullptr || size < 44 || std::memcmp(wav, "RIFF", 4) != 0) {
        ESP_LOGW("Audio", "invalid WAV input");
        return false;
    }
    size_t offset = 12;
    const uint8_t *pcm = nullptr;
    size_t pcm_size = 0;
    uint16_t channels = 0;
    while (offset + 8 <= size) {
        uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, wav + offset + 4, 4);
        if (chunk_size > size - offset - 8) {
            ESP_LOGW("Audio", "invalid WAV chunk");
            return false;
        }
        if (std::memcmp(wav + offset, "fmt ", 4) == 0 && chunk_size >= 16) {
            std::memcpy(&channels, wav + offset + 10, 2);
        } else if (std::memcmp(wav + offset, "data", 4) == 0) {
            pcm = wav + offset + 8;
            pcm_size = std::min(chunk_size, static_cast<uint32_t>(size - offset - 8));
            break;
        }
        offset += 8 + chunk_size + (chunk_size & 1);
    }
    if (pcm == nullptr || pcm_size == 0 || pcm_size % 2 != 0 || (channels != 1 && channels != 2)) {
        ESP_LOGW("Audio", "unsupported PCM data");
        return false;
    }
    ESP_LOGI("Audio", "PCM: %u bytes, %u channels", static_cast<unsigned>(pcm_size), channels);
    if (!playback_configured_) {
        const esp_err_t codec_result = bsp_extra_codec_set_fs(24000, 16, I2S_SLOT_MODE_STEREO);
        if (codec_result != ESP_OK) {
            ESP_LOGW("Audio", "codec setup failed: %s", esp_err_to_name(codec_result));
            return false;
        }
        playback_configured_ = true;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    (void)bsp_extra_codec_mute_set(false);
#if CONFIG_SAGE_ROLE_SUN
    size_t written_total = 0;
    int16_t stereo[1024];
    const int64_t animation_started = esp_timer_get_time();
    while (written_total < pcm_size) {
        if (cancel.load() || button.clicked()) {
            cancel.store(true);
            silence();
            return false;
        }
        const size_t chunk = std::min<size_t>(channels == 1 ? 2048 : 4096,
                                              pcm_size - written_total);
        const uint8_t *source = pcm + written_total;
        size_t output_size = chunk;
        if (channels == 1) {
            const auto *mono = reinterpret_cast<const int16_t *>(source);
            for (size_t index = 0; index < chunk / 2; ++index) {
                stereo[index * 2] = mono[index];
                stereo[index * 2 + 1] = mono[index];
            }
            source = reinterpret_cast<const uint8_t *>(stereo);
            output_size *= 2;
        }
        size_t written = 0;
        const esp_err_t result = bsp_extra_i2s_write(
            const_cast<uint8_t *>(source), output_size, &written, portMAX_DELAY);
        if (result != ESP_OK || written != output_size) {
            ESP_LOGW("Audio", "write failed: %s, %u/%u", esp_err_to_name(result),
                     static_cast<unsigned>(written), static_cast<unsigned>(output_size));
            silence();
            return false;
        }
        written_total += chunk;
        if ((written_total / 4096) % 4 == 0) {
            frame(static_cast<int>((esp_timer_get_time() - animation_started) * 30 / 1000000));
        }
    }
    ESP_LOGI("Audio", "play complete");
    return true;
#else
    Playback playback{pcm, pcm_size, &cancel, channels};
    if (xTaskCreatePinnedToCore(playback_worker, "audio_playback", 8192, &playback, 6, nullptr, 1) != pdPASS) {
        silence();
        return false;
    }
    const int64_t animation_started = esp_timer_get_time();
    while (!playback.done.load()) {
        if (cancel.load() || button.clicked()) {
            cancel.store(true);
            silence();
        } else {
            const int frame_number = static_cast<int>((esp_timer_get_time() - animation_started) * 30 / 1000000);
            frame(frame_number);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (playback.success && !cancel.load()) ESP_LOGI("Audio", "play complete");
    return playback.success && !cancel.load();
#endif
}

void Audio::silence()
{
    if (ready_) {
        (void)bsp_extra_codec_mute_set(true);
    }
}
