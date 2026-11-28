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

/* 当前生效的凭据：屏的连接页要显示真实热点名（0x22 INFO），不是写死的占位串 */
static char s_ssid[33];
static char s_pass[65];
static uint8_t s_ready;

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

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_ready = 1;

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
        /* M10-F-06：口令不进日志（以前是 pass=%s）。串口日志常被收进证据文档长期留存。
         * 口令本身不是机密、NVS 也不加密——这两条写在正典《现行规格说明书》§6，量产加密列为后续项。*/
        ESP_LOGI(TAG, "AP \"%s\" up, IP " IPSTR " pass=***",
                 ssid, IP2STR(&ip.ip));
    }
}

int softap_set_credentials(const char *ssid, const char *pass)
{
    if (!s_ready || !ssid || !pass) return -1;
    size_t sl = strlen(ssid), pl = strlen(pass);
    if (sl < 1 || sl > 32 || pl < 8 || pl > 63) return -2;

    nvs_handle_t h;
    if (nvs_open("tcs", NVS_READWRITE, &h) != ESP_OK) return -3;
    esp_err_t e = nvs_set_str(h, "ssid", ssid);
    if (e == ESP_OK) e = nvs_set_str(h, "pass", pass);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) return -3;

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)sl;
    strncpy((char *)ap.ap.password, pass, sizeof(ap.ap.password));
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.channel = 6;
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) return -4;

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = 0;
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = 0;
    ESP_LOGI(TAG, "AP credentials changed (ssid=%s, len=%u), clients will reconnect", ssid, (unsigned)sl);
    return 0;
}

int softap_credentials(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    if (!s_ready || !ssid || !pass || ssid_cap < 2 || pass_cap < 2) return -1;
    if (strlen(s_ssid) + 1u > ssid_cap || strlen(s_pass) + 1u > pass_cap) return -1;
    strcpy(ssid, s_ssid);
    strcpy(pass, s_pass);
    return 0;
}
