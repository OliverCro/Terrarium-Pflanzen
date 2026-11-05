#include "http_server.h"

#include <stdlib.h>
#include <string.h>

#include "terrarium_state.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"

#define TAG "http_server"

#define HTTP_MAX_OPEN_SOCKETS 12
#define HTTP_SERVER_STACK_SIZE 8192
/* Keep the single httpd task responsive: a stalled client must not block the
 * captive portal for the default 5 seconds. */
#define HTTP_SEND_TIMEOUT_S 1
#define HTTP_RECV_TIMEOUT_S 2
#define HTTP_KEEPALIVE_IDLE_S 30
#define HTTP_KEEPALIVE_INTERVAL_S 5
#define HTTP_KEEPALIVE_COUNT 3
#define WS_MAX_PAYLOAD 1024
#define WS_DISCARD_CHUNK 128
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

static esp_err_t captive_redirect_handler(httpd_req_t *req)
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

/* Consume and throw away a frame that is too large for us. Leaving the bytes
 * unread desynchronises the WebSocket stream, so the socket can never be used
 * again afterwards. The scratch buffer is owned by the caller so the frame
 * never keeps a pointer to a dead stack frame. */
static esp_err_t ws_discard_payload(httpd_req_t *req, httpd_ws_frame_t *frame,
                                    uint8_t *scratch, size_t scratch_size)
{
    frame->payload = scratch;
    while (frame->left_len > 0) {
        size_t chunk = frame->left_len < scratch_size
                           ? frame->left_len
                           : scratch_size;

        esp_err_t err = httpd_ws_recv_frame_part(req, frame, chunk);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    /* A GET here means the WebSocket handshake just completed. */
    if (req->method == HTTP_GET) {
        char *state = terrarium_state_json();
        if (state != NULL) {
            esp_err_t err = ws_send_json(req, state);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "failed to send state: %s",
                         esp_err_to_name(err));
            }
            free(state);
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
        uint8_t scratch[WS_DISCARD_CHUNK];
        return ws_discard_payload(req, &frame, scratch, sizeof(scratch));
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
        char *state = terrarium_state_handle_client_json((const char *)payload);
        if (state != NULL) {
            http_server_broadcast_text(state);
            free(state);
        }
    }

    free(payload);
    return ESP_OK;
}

/* Check without blocking whether a WebSocket client can accept data. A client
 * whose TCP send buffer is full (asleep phone, gone Chromebook, ...) would
 * otherwise stall the httpd task for the whole send timeout. */
static bool ws_client_writable(int fd)
{
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
    int ret = select(fd + 1, NULL, &write_fds, NULL, &timeout);

    return ret > 0 && FD_ISSET(fd, &write_fds);
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
            int fd = client_fds[i];

            if (httpd_ws_get_fd_info(work->server, fd) !=
                HTTPD_WS_CLIENT_WEBSOCKET) {
                continue;
            }

            if (!ws_client_writable(fd)) {
                ESP_LOGW(TAG, "client fd %d is not reading, skipping message",
                         fd);
                continue;
            }

            esp_err_t err =
                httpd_ws_send_frame_async(work->server, fd, &frame);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "send to fd %d failed: %s, closing session", fd,
                         esp_err_to_name(err));
                /* Make sure the LRU bookkeeping does not classify this as a
                 * zeroed, already-reused slot, then close it from the work
                 * queue (httpd tasks must not delete sessions directly). */
                httpd_sess_update_lru_counter(work->server, fd);
                httpd_sess_trigger_close(work->server, fd);
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
        .method = HTTP_ANY,
        .handler = captive_redirect_handler,
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
    config.send_wait_timeout = HTTP_SEND_TIMEOUT_S;
    config.recv_wait_timeout = HTTP_RECV_TIMEOUT_S;
    config.keep_alive_enable = true;
    config.keep_alive_idle = HTTP_KEEPALIVE_IDLE_S;
    config.keep_alive_interval = HTTP_KEEPALIVE_INTERVAL_S;
    config.keep_alive_count = HTTP_KEEPALIVE_COUNT;

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
