#pragma once

#include <stdint.h>

/* Start a wildcard DNS responder on UDP port 53. Every A query is answered
 * with ap_ip_addr (network byte order, e.g. ip.ip.addr from esp_netif). */
void dns_captive_start(uint32_t ap_ip_addr);
