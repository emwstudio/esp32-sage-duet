#include "app_state.hpp"
#include "audio.hpp"
#include "button.hpp"
#include "classic_shaolan_audio.hpp"
#include "conversation_client.hpp"
#include "esp_now_link.hpp"
#include "ui.hpp"
#include "wifi.hpp"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "nvs_flash.h"
#include <atomic>
#include <cstring>

namespace {
constexpr const char *TAG = "SageHost";
constexpr size_t DRAW_BUFFER_HEIGHT = 12;
constexpr size_t MAX_TRANSFER_SIZE = BSP_LCD_H_RES * DRAW_BUFFER_HEIGHT * BSP_LCD_BITS_PER_PIXEL / 8;
constexpr int WAIT_TIMEOUT_MS = 120000;

Button button;
Audio audio;
Ui ui;
ConversationClient client;
uint8_t *audio_buffers[2] = {};
size_t audio_size = 0;
std::atomic<bool> cancel_requested{false};
std::atomic<uint32_t> peer_ready_turn{0};
std::atomic<uint32_t> peer_done_turn{0};
int state = 0;
TickType_t waiting_started = 0;

struct PairJob {
    int after_turn = 0;
    int slot = 0;
    Turn alan;
    Turn sun;
    size_t audio_size = 0;
    bool success = false;
    bool cancelled = false;
    std::atomic<bool> done{false};
};
PairJob *job = nullptr;

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
    const std::string incoming(message.session_id);
    switch (message.type) {
    case esp_now_link::MessageType::Hello:
        ESP_LOGI(TAG, "SunPet online");
        break;
    case esp_now_link::MessageType::Ready:
        if (incoming != client.session_id()) {
            ESP_LOGW(TAG, "ready session mismatch: incoming='%s' current='%s'", incoming.c_str(), client.session_id().c_str());
        } else {
            peer_ready_turn.store(message.turn);
        }
        break;
    case esp_now_link::MessageType::Done:
        if (incoming != client.session_id()) {
            ESP_LOGW(TAG, "done session mismatch: incoming='%s' current='%s'", incoming.c_str(), client.session_id().c_str());
        } else {
            peer_done_turn.store(message.turn);
        }
        break;
    case esp_now_link::MessageType::Stop:
        cancel_requested.store(true);
        break;
    default:
        break;
    }
}

void pair_worker(void *argument)
{
    PairJob *work = static_cast<PairJob *>(argument);
    work->success = work->after_turn == 0
        ? client.create_auto(work->alan)
        : client.next(work->after_turn, work->alan);
    if (work->success && !work->cancelled) {
        work->audio_size = classic_shaolan_audio(work->alan.number, audio_buffers[work->slot], AUDIO_SLOT_BYTES);
        work->success = work->audio_size != 0;
    }
    if (work->success && !work->cancelled) work->success = client.next(work->alan.number, work->sun);
    while (work->after_turn > 0 && peer_done_turn.load() != work->after_turn && !work->cancelled) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (work->success && !work->cancelled) {
        peer_ready_turn.store(0);
        work->success = esp_now_link::prepare(client.session_id(), work->sun.number, work->sun.text);
    }
    work->done.store(true);
    vTaskDelete(nullptr);
}

bool start_pair(int after_turn, int slot)
{
    if (job != nullptr) return false;
    job = new PairJob();
    job->after_turn = after_turn;
    job->slot = slot;
    waiting_started = xTaskGetTickCount();
    if (xTaskCreatePinnedToCore(pair_worker, "sage_pair", 16 * 1024, job, 4, nullptr, 0) != pdPASS) {
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
    esp_now_link::stop();
    client.cancel();
    state = 0;
    ui.idle(0);
}

void show_error(const char *message)
{
    ESP_LOGW(TAG, "%s: %s", message, client.last_error());
    state = 3;
    ui.error(message);
}

void play_pair(const Turn &next_alan, const Turn &next_sun, size_t ready_audio_size, int slot)
{
    const Turn alan = next_alan;
    const Turn sun = next_sun;
    audio_size = ready_audio_size;
    discard_job();
    cancel_requested.store(false);
    if (!start_pair(sun.number, 1 - slot)) {
        show_error("无法预下载下一对语录");
        return;
    }
    ui.status("shaolan", "正在接话", alan.text);
    ESP_LOGI(TAG, "Alan audio start turn %lu", static_cast<unsigned long>(alan.number));
    const bool completed = audio.play(audio_buffers[slot], audio_size, button, cancel_requested, [&alan](int frame) {
        ui.playing("shaolan", alan.text, frame);
    });
    if (!completed || cancel_requested.load()) {
        stop_dialogue();
        return;
    }
    ui.status("shaolan", "等待太对了", alan.text);
    ESP_LOGI(TAG, "Alan done, play Sun turn %lu", static_cast<unsigned long>(sun.number));
    peer_done_turn.store(0);
    vTaskDelay(pdMS_TO_TICKS(250));
    if (!esp_now_link::play(sun.number)) {
        show_error("太对了播放通知失败");
        return;
    }
    state = 2;
}

void update_dialogue()
{
    if (state == 0 || state == 3 || job == nullptr) return;
    if (job->done.load()) {
        if (!job->success) {
            show_error("语录或音频准备失败");
            discard_job();
            return;
        }
        const bool peer_ready = peer_ready_turn.load() == job->sun.number;
        const bool peer_done = state == 1 || peer_done_turn.load() == job->after_turn;
        if (peer_ready && peer_done) {
            play_pair(job->alan, job->sun, job->audio_size, job->slot);
            return;
        }
    }
    if (xTaskGetTickCount() - waiting_started > pdMS_TO_TICKS(WAIT_TIMEOUT_MS)) {
        show_error("等待太对了或后端超时");
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
    for (auto &buffer : audio_buffers) {
        buffer = static_cast<uint8_t *>(heap_caps_malloc(AUDIO_SLOT_BYTES, MALLOC_CAP_SPIRAM));
    }
    if (audio_buffers[0] == nullptr || audio_buffers[1] == nullptr || !audio.init()) {
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
    if (client.health()) {
        ESP_LOGI(TAG, "Backend and ESP-NOW ready");
    } else {
        ESP_LOGW(TAG, "Backend check failed: %s", client.last_error());
    }
    ui.idle(0);

    while (true) {
        if (cancel_requested.load() && state != 0) stop_dialogue();
        if (state == 0) {
            discard_job();
            if (button.clicked()) {
                if (job != nullptr) {
                    ui.error("正在结束上一会话");
                } else {
                    cancel_requested.store(false);
                    peer_ready_turn.store(0);
                    peer_done_turn.store(0);
                    if (start_pair(0, 0)) {
                        state = 1;
                        ui.status("shaolan", "准备经典语录", "等待太对了就绪");
                    }
                }
            }
        } else if (state == 3) {
            if (button.clicked()) stop_dialogue();
        } else {
            if (button.clicked()) {
                stop_dialogue();
            } else {
                update_dialogue();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
