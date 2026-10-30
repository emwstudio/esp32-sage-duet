#include "app_state.hpp"
#include "audio.hpp"
#include "../../firmware/main/button.hpp"
#include "classic_sun_audio.hpp"
#include "conversation_client.hpp"
#include "esp_now_link.hpp"
#include "ui.hpp"
#include "wifi.hpp"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lv_adapter.h"
#include "nvs_flash.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace {
constexpr const char *TAG = "SageSun";
constexpr size_t DRAW_BUFFER_HEIGHT = 12;
constexpr size_t MAX_TRANSFER_SIZE = BSP_LCD_H_RES * DRAW_BUFFER_HEIGHT * BSP_LCD_BITS_PER_PIXEL / 8;

Button button;
Audio audio;
Ui ui;
ConversationClient client;
uint8_t *audio_buffer = nullptr;
size_t audio_size = 0;
std::atomic<bool> cancel_requested{false};
std::atomic<bool> play_requested{false};
std::atomic<bool> prepare_pending{false};
std::atomic<int> state{0};
std::mutex pending_mutex;
std::string pending_session;
std::string pending_text;
uint32_t pending_turn = 0;
std::string session_id;
std::string text;
uint32_t turn = 0;
TickType_t last_hello = 0;

struct DownloadJob {
    uint32_t turn = 0;
    size_t audio_size = 0;
    bool success = false;
    bool cancelled = false;
    std::atomic<bool> done{false};
};
DownloadJob *job = nullptr;

void round_flush_area(lv_event_t *event)
{
    auto *area = static_cast<lv_area_t *>(lv_event_get_param(event));
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

lv_display_t *start_display()
{
    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter.task_stack_size = 20 * 1024;
    adapter.stack_in_psram = false;
    if (esp_lv_adapter_init(&adapter) != ESP_OK) return nullptr;
    const bsp_display_config_t panel_config = {.max_transfer_sz = MAX_TRANSFER_SIZE};
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    if (bsp_display_new(&panel_config, &panel, &panel_io) != ESP_OK) return nullptr;
    auto display_config = ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
        panel, panel_io, BSP_LCD_H_RES, BSP_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = DRAW_BUFFER_HEIGHT;
    lv_display_t *display = esp_lv_adapter_register_display(&display_config);
    if (display == nullptr) return nullptr;
    lv_display_add_event_cb(display, round_flush_area, LV_EVENT_INVALIDATE_AREA, nullptr);
    if (bsp_display_brightness_init() != ESP_OK) return nullptr;
    if (bsp_display_backlight_off() != ESP_OK || esp_lv_adapter_start() != ESP_OK) return nullptr;
    return display;
}

void receive_link(const esp_now_link::Message &message)
{
    if (message.type == esp_now_link::MessageType::Prepare) {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending_session = message.session_id;
        pending_turn = message.turn;
        pending_text.assign(message.text, message.text_length);
        prepare_pending.store(true);
    } else if (message.type == esp_now_link::MessageType::Play) {
        if (state.load() == 2 && message.turn == turn && session_id == message.session_id) {
            play_requested.store(true);
        }
    } else if (message.type == esp_now_link::MessageType::Stop) {
        cancel_requested.store(true);
    }
}

size_t upmix_embedded(uint8_t *wav, size_t size, size_t capacity)
{
    if (size < 44) return 0;
    size_t offset = 12;
    size_t format_offset = 0;
    while (offset + 8 <= size) {
        uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, wav + offset + 4, 4);
        if (chunk_size > size - offset - 8) return 0;
        if (std::memcmp(wav + offset, "fmt ", 4) == 0) {
            uint16_t channels = 0;
            std::memcpy(&channels, wav + offset + 10, 2);
            if (channels != 1) return channels == 2 ? size : 0;
            format_offset = offset;
        } else if (std::memcmp(wav + offset, "data", 4) == 0) {
            if (format_offset == 0 || chunk_size % 2 != 0 || size + chunk_size > capacity) return 0;
            auto *samples = reinterpret_cast<int16_t *>(wav + offset + 8);
            const size_t frames = chunk_size / 2;
            for (size_t index = frames; index-- > 0;) {
                samples[index * 2] = samples[index];
                samples[index * 2 + 1] = samples[index];
            }
            const uint16_t channels = 2;
            const uint16_t block_align = 4;
            const uint32_t byte_rate = 24000 * 4;
            const uint32_t data_size = chunk_size * 2;
            uint32_t riff_size = 0;
            std::memcpy(&riff_size, wav + 4, 4);
            riff_size += chunk_size;
            std::memcpy(wav + 4, &riff_size, 4);
            std::memcpy(wav + format_offset + 10, &channels, 2);
            std::memcpy(wav + format_offset + 16, &byte_rate, 4);
            std::memcpy(wav + format_offset + 20, &block_align, 2);
            std::memcpy(wav + offset + 4, &data_size, 4);
            return size + chunk_size;
        }
        offset += 8 + chunk_size + (chunk_size & 1);
    }
    return 0;
}

void download_worker(void *argument)
{
    DownloadJob *work = static_cast<DownloadJob *>(argument);
    const uint8_t *embedded = nullptr;
    const size_t embedded_size = classic_sun_audio(work->turn, &embedded);
    work->success = embedded != nullptr && embedded_size <= AUDIO_SLOT_BYTES;
    if (work->success) {
        std::memcpy(audio_buffer, embedded, embedded_size);
        work->audio_size = upmix_embedded(audio_buffer, embedded_size, AUDIO_SLOT_BYTES);
        work->success = work->audio_size != 0;
    }
    work->done.store(true);
    vTaskDelete(nullptr);
}

bool start_download()
{
    if (job != nullptr) return false;
    job = new DownloadJob();
    job->turn = turn;
    if (xTaskCreatePinnedToCore(download_worker, "sun_audio", 16 * 1024, job, 4, nullptr, 0) != pdPASS) {
        delete job;
        job = nullptr;
        return false;
    }
    return true;
}

void discard_job()
{
    if (job != nullptr && job->done.load()) {
        delete job;
        job = nullptr;
    }
}

void stop_dialogue()
{
    cancel_requested.store(true);
    if (job != nullptr) job->cancelled = true;
    audio.silence();
    client.cancel();
    session_id.clear();
    turn = 0;
    prepare_pending.store(false);
    play_requested.store(false);
    state.store(0);
    ui.idle(0);
}

void accept_prepare()
{
    std::lock_guard<std::mutex> lock(pending_mutex);
    session_id = pending_session;
    turn = pending_turn;
    text = pending_text;
    prepare_pending.store(false);
    cancel_requested.store(false);
    client.adopt_session(session_id);
    esp_now_link::adopt_session(session_id);
    if (start_download()) {
        state.store(1);
        ui.status("sun", "缓存太对了", text.c_str());
    }
}
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        result = nvs_flash_init();
    }
    if (result != ESP_OK) return;
    lv_display_t *display = start_display();
    if (display == nullptr) return;
    button.init();
    if (bsp_display_lock(-1) == ESP_OK) {
        const bool ui_ok = ui.begin();
        if (ui_ok) ui.idle(0);
        bsp_display_unlock();
        if (!ui_ok) return;
    }
    (void)esp_lv_adapter_refresh_now(display);
    bsp_display_backlight_on();
    audio_buffer = static_cast<uint8_t *>(heap_caps_malloc(AUDIO_SLOT_BYTES, MALLOC_CAP_SPIRAM));
    if (audio_buffer == nullptr || !audio.init()) {
        ui.error("内存或音频初始化失败");
        return;
    }
    ESP_LOGI(TAG, "display and audio ready");
    if (!wifi_connect()) {
        ui.error("Wi-Fi 连接失败");
        return;
    }
    if (wifi_channel() != CONFIG_SAGE_ESPNOW_CHANNEL) {
        ui.error("Wi-Fi 信道与 ESP-NOW 不一致");
        return;
    }
    if (!esp_now_link::begin(CONFIG_SAGE_PEER_MAC, CONFIG_SAGE_ESPNOW_CHANNEL, receive_link)) {
        ui.error("ESP-NOW 初始化失败");
        return;
    }
    ui.idle(0);
    last_hello = xTaskGetTickCount();

    while (true) {
        if (cancel_requested.load() && state.load() != 0) stop_dialogue();
        if (button.clicked() && state.load() != 0) stop_dialogue();
        if (prepare_pending.load() && state.load() != 3 && job == nullptr) accept_prepare();
        if (job != nullptr && job->done.load()) {
            if (!job->cancelled && state.load() == 1 && job->turn == turn) {
                if (job->success) {
                    audio_size = job->audio_size;
                    discard_job();
                    state.store(2);
                    ui.status("sun", "太对了已就绪", text.c_str());
                    ESP_LOGI(TAG, "ready turn %lu", static_cast<unsigned long>(turn));
                    esp_now_link::ready(turn);
                } else {
                    discard_job();
                    state.store(4);
                    ui.error(client.last_error());
                }
            } else {
                discard_job();
            }
        }
        if (play_requested.exchange(false) && state.load() == 2) {
            state.store(3);
            cancel_requested.store(false);
            const int64_t started = esp_timer_get_time();
            ESP_LOGI(TAG, "play turn %lu", static_cast<unsigned long>(turn));
            const bool completed = audio.play(audio_buffer, audio_size, button, cancel_requested, [](int frame) {
                ui.playing("sun", text.c_str(), frame);
            });
            ESP_LOGI(TAG, "playback start-to-end %lld ms", static_cast<long long>((esp_timer_get_time() - started) / 1000));
            if (!completed || cancel_requested.load()) {
                stop_dialogue();
                continue;
            }
            ui.status("sun", "等待下一句", text.c_str());
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_now_link::done(turn);
            state.store(2);
        }
        if (state.load() == 0 && xTaskGetTickCount() - last_hello > pdMS_TO_TICKS(5000)) {
            last_hello = xTaskGetTickCount();
            esp_now_link::hello();
        }
        discard_job();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
