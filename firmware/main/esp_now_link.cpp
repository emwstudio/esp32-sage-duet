#include "esp_now_link.hpp"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char *TAG = "EspNowLink";
constexpr uint16_t MAGIC = 0x5347;
constexpr uint8_t VERSION = 1;
constexpr int MAX_ATTEMPTS = 5;
constexpr int RETRY_MS = 150;

uint8_t peer_address[6] = {};
esp_now_link::Receiver on_message;
std::string session_id;
std::atomic<uint32_t> next_sequence{1};
std::atomic<uint32_t> acknowledged_sequence{0};
uint32_t recent_sequences[16] = {};
size_t recent_index = 0;

bool parse_mac(const char *text, uint8_t *address)
{
    unsigned int values[6] = {};
    if (text == nullptr || std::sscanf(text, "%x:%x:%x:%x:%x:%x",
            &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6) {
        return false;
    }
    for (int index = 0; index < 6; ++index) address[index] = values[index];
    return true;
}

bool seen(uint32_t sequence)
{
    for (uint32_t item : recent_sequences) {
        if (item == sequence) return true;
    }
    recent_sequences[recent_index] = sequence;
    recent_index = (recent_index + 1) % 16;
    return false;
}

void receive_callback(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (length != sizeof(esp_now_link::Message)) return;
    esp_now_link::Message message;
    std::memcpy(&message, data, sizeof(message));
    if (message.magic != MAGIC || message.version != VERSION || message.session_id[32] != '\0' ||
        message.text_length > sizeof(message.text)) {
        return;
    }
    if (message.type != esp_now_link::MessageType::Ack) {
        esp_now_link::Message ack = message;
        ack.type = esp_now_link::MessageType::Ack;
        ack.text_length = 0;
        esp_now_send(info->src_addr, reinterpret_cast<const uint8_t *>(&ack), sizeof(ack));
    }
    if (message.type == esp_now_link::MessageType::Ack) {
        acknowledged_sequence.store(message.sequence);
        return;
    }
    if (seen(message.sequence)) return;
    ESP_LOGI(TAG, "rx type=%u turn=%lu seq=%lu", static_cast<unsigned>(message.type),
             static_cast<unsigned long>(message.turn), static_cast<unsigned long>(message.sequence));
    if (on_message) on_message(message);
}

void send_callback(const esp_now_send_info_t *, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) ESP_LOGW(TAG, "radio send failed");
}

bool send(esp_now_link::MessageType type, uint32_t turn, const std::string &text = "")
{
    esp_now_link::Message message = {};
    message.magic = MAGIC;
    message.version = VERSION;
    message.type = type;
    message.sequence = next_sequence.fetch_add(1);
    message.turn = turn;
    std::snprintf(message.session_id, sizeof(message.session_id), "%s", session_id.c_str());
    message.text_length = std::min(text.size(), sizeof(message.text));
    if (message.text_length > 0) std::memcpy(message.text, text.data(), message.text_length);
    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        if (esp_now_send(peer_address, reinterpret_cast<const uint8_t *>(&message), sizeof(message)) != ESP_OK) {
            return false;
        }
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(RETRY_MS);
        while (xTaskGetTickCount() < deadline) {
            if (acknowledged_sequence.load() == message.sequence) {
                ESP_LOGI(TAG, "tx type=%u turn=%lu seq=%lu", static_cast<unsigned>(type),
                         static_cast<unsigned long>(turn), static_cast<unsigned long>(message.sequence));
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    ESP_LOGW(TAG, "ack timeout type=%u turn=%lu", static_cast<unsigned>(type), static_cast<unsigned long>(turn));
    return false;
}
}

namespace esp_now_link {

bool begin(const char *peer_mac, uint8_t channel, Receiver receiver)
{
    if (!parse_mac(peer_mac, peer_address)) return false;
    on_message = std::move(receiver);
    uint8_t current_channel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&current_channel, &secondary) != ESP_OK) return false;
    if (current_channel != channel && esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_send_cb(send_callback);
    esp_now_register_recv_cb(receive_callback);
    esp_now_peer_info_t peer = {};
    std::memcpy(peer.peer_addr, peer_address, sizeof(peer_address));
    peer.channel = channel;
    peer.ifidx = WIFI_IF_STA;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool hello()
{
    return send(MessageType::Hello, 0);
}

void adopt_session(const std::string &id)
{
    if (id != session_id) {
        std::fill(std::begin(recent_sequences), std::end(recent_sequences), 0);
        recent_index = 0;
        acknowledged_sequence.store(0);
    }
    session_id = id;
}

bool prepare(const std::string &id, uint32_t turn, const std::string &text)
{
    adopt_session(id);
    return send(MessageType::Prepare, turn, text);
}

bool ready(uint32_t turn)
{
    return send(MessageType::Ready, turn);
}

bool play(uint32_t turn)
{
    return send(MessageType::Play, turn);
}

bool done(uint32_t turn)
{
    return send(MessageType::Done, turn);
}

bool stop()
{
    return send(MessageType::Stop, 0);
}

}
