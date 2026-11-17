#include "wifi_manager.h"
#include "config/config.h"

#include <cstring>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

WiFiManager::WiFiManager() {
    _apSSID[0] = '\0';
}

bool WiFiManager::setupAP() {
    log_line("=== Starting WiFi AP Setup ===");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init_cfg) != ESP_OK) {
        log_line("ERROR: WiFi init failed");
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_AP);

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(_apSSID, sizeof(_apSSID), "%s-%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
    log_fmt("AP MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_config_t wifi = {};
    std::strncpy((char*)wifi.ap.ssid, _apSSID, sizeof(wifi.ap.ssid) - 1);
    wifi.ap.ssid_len = std::strlen(_apSSID);
    wifi.ap.channel = 1;
    wifi.ap.authmode = WIFI_AUTH_OPEN;
    wifi.ap.max_connection = 4;
    wifi.ap.beacon_interval = 100;

    esp_netif_ip_info_t ip_info = {};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    if (esp_wifi_set_config(WIFI_IF_AP, &wifi) != ESP_OK || esp_wifi_start() != ESP_OK) {
        log_line("ERROR: WiFi AP failed to start");
        return false;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    log_line("=== WiFi AP Started ===");
    log_fmt("SSID: %s\n", _apSSID);
    log_line("IP: 192.168.4.1");
    return true;
}
