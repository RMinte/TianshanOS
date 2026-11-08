#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lwip/ip_addr.h"

/* Worker tasks only, never the TCP/IP thread. No additional resolver task is
 * created. At most one unresolved callback is allocated, even after its caller
 * times out/cancels. Callers still own their worker/request admission budget.
 * DNS supplies an address, not server identity or permission to contact it. */
esp_err_t ts_dns_resolve(const char *hostname, int64_t deadline_us,
    bool (*still_current)(void *), void *context, ip_addr_t *address);
