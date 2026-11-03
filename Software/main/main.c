#include "chat_store.h"
#include "dns_captive.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "http_server.h"
#include "nvs_flash.h"
#include "wifi_ap.h"

#define TAG "main"

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_init();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = NULL;
    ESP_ERROR_CHECK(wifi_ap_start(&ap_netif));

    chat_store_init();

    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));

    dns_captive_start(ip_info.ip.addr);

    ESP_ERROR_CHECK(http_server_start());

    ESP_LOGI(TAG, "Chat room ready: http://" IPSTR, IP2STR(&ip_info.ip));
}
