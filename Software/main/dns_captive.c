#include "dns_captive.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define TAG "dns_captive"

#define DNS_PORT 53
#define DNS_BUF_SIZE 512
#define DNS_TASK_STACK_SIZE 4096
#define DNS_TASK_PRIORITY 5

static uint32_t s_ap_ip_addr; /* network byte order */

/* Build a DNS response for a single-question query. Returns the response
 * length, or -1 if the query is malformed / not supported. */
static int build_response(const uint8_t *query, size_t query_len,
                          uint8_t *response, size_t response_size)
{
    if (query_len < 12 || response_size < 12) {
        return -1;
    }

    uint16_t question_count = ((uint16_t)query[4] << 8) | query[5];
    if (question_count == 0) {
        return -1;
    }

    /* Walk the QNAME labels up to the terminating zero byte. */
    size_t pos = 12;
    bool terminated = false;

    while (pos < query_len) {
        uint8_t label_len = query[pos];

        if (label_len == 0) {
            pos++;
            terminated = true;
            break;
        }
        if ((label_len & 0xC0) != 0) {
            /* Compression pointers are not valid in a question. */
            return -1;
        }
        pos += (size_t)label_len + 1;
    }

    if (!terminated || pos + 4 > query_len) {
        return -1;
    }

    uint16_t query_type = ((uint16_t)query[pos] << 8) | query[pos + 1];
    size_t question_end = pos + 4; /* QTYPE + QCLASS */

    if (question_end > response_size) {
        return -1;
    }

    memcpy(response, query, question_end);

    /* QR=1, AA=1, preserve RD, RA=1, RCODE=0 */
    response[2] = 0x80 | 0x04 | (query[2] & 0x01);
    response[3] = 0x80;

    response[4] = 0x00;
    response[5] = 0x01; /* QDCOUNT */
    response[8] = response[9] = response[10] = response[11] = 0;

    size_t out = question_end;

    if (query_type == 1 /* A */ && out + 16 <= response_size) {
        response[6] = 0x00;
        response[7] = 0x01; /* ANCOUNT */

        response[out++] = 0xC0; /* name pointer to offset 12 */
        response[out++] = 0x0C;
        response[out++] = 0x00; /* TYPE A */
        response[out++] = 0x01;
        response[out++] = 0x00; /* CLASS IN */
        response[out++] = 0x01;
        response[out++] = 0x00; /* TTL 60s */
        response[out++] = 0x00;
        response[out++] = 0x00;
        response[out++] = 0x3C;
        response[out++] = 0x00; /* RDLENGTH 4 */
        response[out++] = 0x04;
        memcpy(&response[out], &s_ap_ip_addr, 4);
        out += 4;
    } else {
        response[6] = 0x00;
        response[7] = 0x00; /* no answer, but a valid empty response */
    }

    return (int)out;
}

static void dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "wildcard DNS listening on UDP port %d", DNS_PORT);

    uint8_t query[DNS_BUF_SIZE];
    uint8_t response[DNS_BUF_SIZE];

    while (1) {
        struct sockaddr_in source;
        socklen_t source_len = sizeof(source);

        int received = recvfrom(sock, query, sizeof(query), 0,
                                (struct sockaddr *)&source, &source_len);
        if (received <= 0) {
            continue;
        }

        int response_len = build_response(query, (size_t)received,
                                          response, sizeof(response));
        if (response_len > 0) {
            sendto(sock, response, (size_t)response_len, 0,
                   (struct sockaddr *)&source, source_len);
        }
    }
}

void dns_captive_start(uint32_t ap_ip_addr)
{
    s_ap_ip_addr = ap_ip_addr;

    if (xTaskCreate(dns_task, "dns_captive", DNS_TASK_STACK_SIZE, NULL,
                    DNS_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create DNS task");
    }
}
