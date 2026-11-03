#include "http_server.h"

#include <stdlib.h>
#include <string.h>

#include "chat_store.h"
#include "esp_http_server.h"
#include "esp_log.h"

#define TAG "http_server"

#define HTTP_MAX_OPEN_SOCKETS 12
#define HTTP_SERVER_STACK_SIZE 8192
#define WS_MAX_PAYLOAD 1024
#define WS_MAX_CLIENTS 16

/* Embedded web assets (see EMBED_TXTFILES in main/CMakeLists.txt). */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");

static httpd_handle_t s_server;

typedef struct {
    httpd_handle_t server;
    char *json;
} broadcast_work_t;

static esp_err_t send_embedded(httpd_req_t *req, const char *mime_type,
                               const uint8_t *data)
{
    httpd_resp_set_type(req, mime_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)data, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    return send_embedded(req, "text/html; charset=utf-8", index_html_start);
}

static esp_err_t app_js_get_handler(httpd_req_t *req)
{
    return send_embedded(req, "application/javascript", app_js_start);
}

static esp_err_t style_css_get_handler(httpd_req_t *req)
{
    return send_embedded(req, "text/css; charset=utf-8", style_css_start);
}

static esp_err_t not_found_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t ws_send_json(httpd_req_t *req, const char *json)
{
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    return httpd_ws_send_frame(req, &frame);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    /* A GET here means the WebSocket handshake just completed. */
    if (req->method == HTTP_GET) {
        char *history = chat_history_json();
        if (history != NULL) {
            esp_err_t err = ws_send_json(req, history);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "failed to send history: %s",
                         esp_err_to_name(err));
            }
            free(history);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.len == 0) {
        return ESP_OK;
    }

    if (frame.len > WS_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "dropping oversized WebSocket frame (%u bytes)",
                 (unsigned)frame.len);
        return ESP_OK;
    }

    uint8_t *payload = malloc(frame.len + 1);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }

    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        free(payload);
        return err;
    }
    payload[frame.len] = '\0';

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        char *message = chat_handle_client_json((const char *)payload);
        if (message != NULL) {
            http_server_broadcast_text(message);
            free(message);
        }
    }

    free(payload);
    return ESP_OK;
}

static void broadcast_work(void *arg)
{
    broadcast_work_t *work = (broadcast_work_t *)arg;
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)work->json,
        .len = strlen(work->json),
    };

    size_t client_count = WS_MAX_CLIENTS;
    int client_fds[WS_MAX_CLIENTS];

    if (httpd_get_client_list(work->server, &client_count, client_fds) ==
        ESP_OK) {
        for (size_t i = 0; i < client_count; i++) {
            if (httpd_ws_get_fd_info(work->server, client_fds[i]) !=
                HTTPD_WS_CLIENT_WEBSOCKET) {
                continue;
            }

            esp_err_t err =
                httpd_ws_send_frame_async(work->server, client_fds[i], &frame);
            if (err != ESP_OK) {
                ESP_LOGD(TAG, "send to fd %d failed: %s", client_fds[i],
                         esp_err_to_name(err));
            }
        }
    }

    free(work->json);
    free(work);
}

void http_server_broadcast_text(const char *json)
{
    if (s_server == NULL || json == NULL) {
        return;
    }

    broadcast_work_t *work = calloc(1, sizeof(*work));
    if (work == NULL) {
        return;
    }

    work->json = strdup(json);
    if (work->json == NULL) {
        free(work);
        return;
    }
    work->server = s_server;

    if (httpd_queue_work(s_server, broadcast_work, work) != ESP_OK) {
        free(work->json);
        free(work);
    }
}

static const httpd_uri_t s_uris[] = {
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    },
    {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = index_get_handler,
    },
    {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = app_js_get_handler,
    },
    {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = style_css_get_handler,
    },
    {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    },
    {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = not_found_handler,
    },
};

esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = HTTP_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.backlog_conn = 8;
    config.stack_size = HTTP_SERVER_STACK_SIZE;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
        err = httpd_register_uri_handler(s_server, &s_uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to register %s: %s", s_uris[i].uri,
                     esp_err_to_name(err));
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }

    ESP_LOGI(TAG, "HTTP server listening on port %d", config.server_port);
    return ESP_OK;
}
