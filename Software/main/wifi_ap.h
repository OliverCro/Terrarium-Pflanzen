#pragma once

#include "esp_err.h"
#include "esp_netif.h"

/* Bring up an open SoftAP named "Terrarium".
 * Requires esp_netif_init() and esp_event_loop_create_default() to have been
 * called first. On success *out_netif receives the AP network interface. */
esp_err_t wifi_ap_start(esp_netif_t **out_netif);
