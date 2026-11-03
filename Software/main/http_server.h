#pragma once

#include "esp_err.h"

/* Start the HTTP server: serves the embedded web UI and the /ws endpoint. */
esp_err_t http_server_start(void);

/* Broadcast a text frame to every connected WebSocket client.
 * The string is copied, so the caller may free it immediately. */
void http_server_broadcast_text(const char *json);
