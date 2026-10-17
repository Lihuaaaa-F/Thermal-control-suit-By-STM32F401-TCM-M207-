/**
 * softap.c — WiFi SoftAP（任务书 §4.1）
 * SSID = TCS-<mac 后 4 位 hex>；密码 12345678（NVS "tcs" 命名空间可改 ssid/pass）。
 */
#include "softap.h"
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "tcs_wifi";

void softap_start(void)
{
    char ssid[32] = {0};
    char pass[64] = "12345678";

    /* NVS 用户覆盖（网页二期设置入口预留）*/
    nvs_handle_t h;
    if (nvs_open("tcs", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(ssid);
        nvs_get_str(h, "ssid", ssid, &len);
        len = sizeof(pass);
        if (nvs_get_str(h, "pass", pass, &len) != ESP_OK) {
            strcpy(pass, "12345678");
        }
        nvs_close(h);
    }
    if (!ssid[0]) {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf(ssid, sizeof(ssid), "TCS-%02X%02X", mac[4], mac[5]);
    }

    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(ssid);
    strncpy((char *)ap.ap.password, pass, sizeof(ap.ap.password));
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.channel = 6;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        ESP_LOGI(TAG, "AP \"%s\" up, IP " IPSTR " pass=%s",
                 ssid, IP2STR(&ip.ip), pass);
    }
}
