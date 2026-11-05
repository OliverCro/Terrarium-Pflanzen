#include "wifi_ap.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define TAG "wifi_ap"

#define WIFI_AP_SSID "Terrarium"
#define WIFI_AP_PASSWORD "" /* open network, same as the Arduino sketch */
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 8

esp_err_t wifi_ap_start(esp_netif_t **out_netif)
{
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    if (netif == NULL) {
        ESP_LOGE(TAG, "failed to create AP network interface");
        return ESP_FAIL;
    }

    esp_netif_set_hostname(netif, WIFI_AP_SSID);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.ap.ssid, WIFI_AP_SSID,
            sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.pmf_cfg.required = false;

    if (strlen(WIFI_AP_PASSWORD) > 0) {
        strlcpy((char *)wifi_config.ap.password, WIFI_AP_PASSWORD,
                sizeof(wifi_config.ap.password));
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    if (out_netif != NULL) {
        *out_netif = netif;
    }

    return ESP_OK;
}
