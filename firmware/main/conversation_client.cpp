#include "conversation_client.hpp"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr const char *TAG = "ConversationClient";

struct ResponseBuffer {
    std::string *text;
    bool overflow = false;
};

std::string bounded(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

void copy_string(char *destination, size_t capacity, const std::string &source)
{
    if (capacity == 0) return;
    std::snprintf(destination, capacity, "%s", source.c_str());
}

esp_err_t receive_json(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto *buffer = static_cast<ResponseBuffer *>(event->user_data);
    if (buffer->text->size() + event->data_len > JSON_RESPONSE_BYTES) {
        buffer->overflow = true;
        return ESP_ERR_NO_MEM;
    }
    buffer->text->append(static_cast<const char *>(event->data), event->data_len);
    return ESP_OK;
}
}

bool ConversationClient::request_json(const char *method, const char *path,
                                      const uint8_t *body, size_t body_size,
                                      const char *content_type, std::string &response)
{
    const std::string url = std::string(CONFIG_SAGE_BACKEND_URL) + path;
    response.clear();
    ResponseBuffer buffer{&response};
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = std::strcmp(path, "/healthz") == 0 ? 5000 : 120000;
    config.buffer_size = 4096;
    config.event_handler = receive_json;
    config.user_data = &buffer;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error_ = "http init failed";
        return false;
    }
    esp_http_client_set_method(client, std::strcmp(method, "GET") == 0 ? HTTP_METHOD_GET : HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", content_type);
    if (body_size > 0) {
        esp_http_client_set_post_field(client, reinterpret_cast<const char *>(body), body_size);
    }
    const int64_t started = esp_timer_get_time();
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "JSON %s: %lld ms, %u bytes", path,
             static_cast<long long>((esp_timer_get_time() - started) / 1000),
             static_cast<unsigned>(response.size()));
    if (result != ESP_OK || buffer.overflow || status < 200 || status >= 300) {
        error_ = "HTTP " + std::to_string(status) + " / " + esp_err_to_name(result);
        ESP_LOGW(TAG, "%s", error_.c_str());
        return false;
    }
    return true;
}

bool ConversationClient::parse_turn(const std::string &json, Turn &turn)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        error_ = "invalid JSON";
        return false;
    }
    const cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "turn");
    if (object == nullptr) object = root;
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(object, "turn");
    const cJSON *duration = cJSON_GetObjectItemCaseSensitive(object, "duration_ms");
    turn.number = cJSON_IsNumber(number) ? number->valueint : 0;
    turn.duration_ms = cJSON_IsNumber(duration) ? duration->valueint : 0;
    copy_string(turn.speaker, sizeof(turn.speaker), bounded(object, "speaker"));
    copy_string(turn.text, sizeof(turn.text), bounded(object, "text"));
    copy_string(turn.audio_url, sizeof(turn.audio_url), bounded(object, "audio_url"));
    cJSON_Delete(root);
    if (turn.number == 0 || turn.speaker[0] == '\0' || turn.audio_url[0] == '\0') {
        error_ = "incomplete turn";
        return false;
    }
    return true;
}

std::string ConversationClient::session_snapshot() const
{
    std::lock_guard<std::mutex> lock(session_mutex_);
    return session_id_;
}

bool ConversationClient::health()
{
    std::string response;
    return request_json("GET", "/healthz", nullptr, 0, "application/json", response);
}

bool ConversationClient::create_auto(Turn &turn)
{
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id_.clear();
    }
    std::string response;
    if (!request_json("POST", "/v1/auto-sessions", nullptr, 0, "application/json", response)) {
        return false;
    }
    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        error_ = "invalid auto create response";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id_ = bounded(root, "session_id");
    }
    cJSON_Delete(root);
    return !session_snapshot().empty() && parse_turn(response, turn);
}

bool ConversationClient::create(const uint8_t *wav, size_t size, Turn &turn)
{
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id_.clear();
    }
    std::string response;
    if (!request_json("POST", "/v1/sessions", wav, size, "audio/wav", response)) {
        return false;
    }
    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        error_ = "invalid create response";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_id_ = bounded(root, "session_id");
    }
    cJSON_Delete(root);
    return !session_snapshot().empty() && parse_turn(response, turn);
}

bool ConversationClient::next(int after_turn, Turn &turn)
{
    const std::string session = session_snapshot();
    if (session.empty()) {
        error_ = "no session";
        return false;
    }
    const std::string path = "/v1/sessions/" + session + "/turns:next";
    char body[64];
    const int length = std::snprintf(body, sizeof(body), "{\"after_turn\":%d}", after_turn);
    std::string response;
    return request_json("POST", path.c_str(), reinterpret_cast<const uint8_t *>(body), length,
                        "application/json", response) && parse_turn(response, turn);
}

bool ConversationClient::download(const char *path, uint8_t *buffer, size_t capacity, size_t &size)
{
    struct DownloadBuffer {
        uint8_t *data;
        size_t capacity;
        size_t size = 0;
        bool overflow = false;
        int64_t started = 0;
    };
    DownloadBuffer download{buffer, capacity};
    size = 0;
    const std::string url = std::string(CONFIG_SAGE_BACKEND_URL) + path;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 30000;
    config.buffer_size = 8192;
    config.event_handler = [](esp_http_client_event_t *event) -> esp_err_t {
        auto *download = static_cast<DownloadBuffer *>(event->user_data);
        if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
            ESP_LOGI(TAG, "WAV connected: %lld ms",
                     static_cast<long long>((esp_timer_get_time() - download->started) / 1000));
            return ESP_OK;
        }
        if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
        if (download->size == 0) {
            ESP_LOGI(TAG, "WAV first data: %lld ms, %d bytes",
                     static_cast<long long>((esp_timer_get_time() - download->started) / 1000), event->data_len);
        }
        if (download->size + event->data_len > download->capacity) {
            download->overflow = true;
            return ESP_ERR_NO_MEM;
        }
        std::memcpy(download->data + download->size, event->data, event->data_len);
        download->size += event->data_len;
        return ESP_OK;
    };
    config.user_data = &download;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return false;
    download.started = esp_timer_get_time();
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "WAV complete: %lld ms, %u bytes",
             static_cast<long long>((esp_timer_get_time() - download.started) / 1000),
             static_cast<unsigned>(download.size));
    if (result != ESP_OK || download.overflow || status != 200 || download.size == 0) {
        error_ = "audio download failed";
        return false;
    }
    size = download.size;
    return true;
}

void ConversationClient::adopt_session(const std::string &id)
{
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_id_ = id;
}

void ConversationClient::cancel()
{
    std::string session;
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session = session_id_;
        session_id_.clear();
    }
    if (session.empty()) return;
    const std::string path = "/v1/sessions/" + session + "/cancel";
    std::string response;
    (void)request_json("POST", path.c_str(), nullptr, 0, "application/json", response);
}
