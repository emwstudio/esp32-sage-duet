#include "wifi.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include <cstring>

namespace {
constexpr int CONNECTED_BIT = BIT0;
EventGroupHandle_t events;
int retries;
}

static void wifi_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW("SageWiFi", "disconnected: reason=%u, attempt=%d", event ? event->reason : 0, retries + 1);
        if (retries++ < 8) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        if (event) {
            ESP_LOGI("SageWiFi", "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        }
        xEventGroupSetBits(events, CONNECTED_BIT);
    }
}

bool wifi_connect()
{
    events = xEventGroupCreate();
    if (events == nullptr) {
        return false;
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK) {
        return false;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr);
    wifi_config_t sta = {};
    std::strncpy(reinterpret_cast<char *>(sta.sta.ssid), CONFIG_SAGE_WIFI_SSID, sizeof(sta.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(sta.sta.password), CONFIG_SAGE_WIFI_PASSWORD, sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = std::strlen(CONFIG_SAGE_WIFI_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_start();
    if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
        return false;
    }
    wifi_scan_config_t scan = {};
    scan.ssid = reinterpret_cast<uint8_t *>(sta.sta.ssid);
    if (esp_wifi_scan_start(&scan, true) == ESP_OK) {
        wifi_ap_record_t access_points[4];
        uint16_t count = 4;
        if (esp_wifi_scan_get_ap_records(&count, access_points) == ESP_OK) {
            for (int index = 0; index < count; ++index) {
                ESP_LOGI("SageWiFi", "found %s: auth=%d, rssi=%d", access_points[index].ssid,
                         access_points[index].authmode, access_points[index].rssi);
            }
        }
    }
    esp_wifi_connect();
    const EventBits_t bits = xEventGroupWaitBits(events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(25000));
    return (bits & CONNECTED_BIT) != 0;
}

int wifi_channel()
{
    uint8_t channel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    return esp_wifi_get_channel(&channel, &secondary) == ESP_OK ? channel : 0;
}
