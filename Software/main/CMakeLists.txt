idf_component_register(
    SRCS "main.c" "wifi_ap.c" "dns_captive.c" "http_server.c" "terrarium_state.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash lwip
    EMBED_TXTFILES "www/index.html" "www/app.js" "www/style.css"
)
