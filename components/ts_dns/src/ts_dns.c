#include "ts_dns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    atomic_uint references;
    atomic_bool wanted;
    int64_t deadline;
    SemaphoreHandle_t completed;
    ip_addr_t address;
    esp_err_t result;
    char hostname[DNS_MAX_NAME_LENGTH];
} resolve_request_t;

static atomic_bool s_query_active;

static void release_request(resolve_request_t *request)
{
    if (atomic_fetch_sub(&request->references, 1) == 1) {
        vSemaphoreDelete(request->completed);
        free(request);
    }
}

static void resolved(const char *name, const ip_addr_t *address, void *context)
{
    (void)name;
    resolve_request_t *request = context;
    request->result = address ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (address) ip_addr_copy(request->address, *address);
    /* The old callback never touches this admission flag after waking a caller
     * that may immediately finish its job and start another resolution. */
    atomic_store(&s_query_active, false);
    xSemaphoreGive(request->completed);
    release_request(request);
}

static void start_query(void *context)
{
    resolve_request_t *request = context;
    if (!atomic_load(&request->wanted) || esp_timer_get_time() >= request->deadline) {
        resolved(NULL, NULL, request);
        return;
    }
    err_t result = dns_gethostbyname_addrtype(request->hostname, &request->address,
        resolved, request, LWIP_DNS_ADDRTYPE_DEFAULT);
    if (result == ERR_OK) resolved(NULL, &request->address, request);
    else if (result != ERR_INPROGRESS) resolved(NULL, NULL, request);
}

static esp_err_t allowed(int64_t deadline, bool (*still_current)(void *), void *context)
{
    if (still_current && !still_current(context)) return ESP_ERR_INVALID_STATE;
    return esp_timer_get_time() < deadline ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ts_dns_resolve(const char *hostname, int64_t deadline,
    bool (*still_current)(void *), void *context, ip_addr_t *address)
{
    if (!address) return ESP_ERR_INVALID_ARG;
    ip_addr_set_zero(address);
    if (!hostname) return ESP_ERR_INVALID_ARG;
    size_t length = strnlen(hostname, DNS_MAX_NAME_LENGTH);
    if (!length || length == DNS_MAX_NAME_LENGTH) return ESP_ERR_INVALID_ARG;
    esp_err_t result = allowed(deadline, still_current, context);
    if (result != ESP_OK) return result;
    ip_addr_t literal;
    if (ipaddr_aton(hostname, &literal)) { ip_addr_copy(*address, literal); return ESP_OK; }
    for (;;) {
        result = allowed(deadline, still_current, context);
        if (result != ESP_OK) return result;
        bool available = false;
        if (atomic_compare_exchange_strong(&s_query_active, &available, true)) break;
        vTaskDelay(1);
    }
    resolve_request_t *request = calloc(1, sizeof(*request));
    if (!request) { atomic_store(&s_query_active, false); return ESP_ERR_NO_MEM; }
    request->completed = xSemaphoreCreateBinary();
    if (!request->completed) { free(request); atomic_store(&s_query_active, false); return ESP_ERR_NO_MEM; }
    atomic_init(&request->references, 2); /* caller plus queued/DNS callback */
    atomic_init(&request->wanted, true);
    request->deadline = deadline;
    memcpy(request->hostname, hostname, length);
    request->hostname[length] = 0;
    if (tcpip_try_callback(start_query, request) != ERR_OK) {
        atomic_store(&s_query_active, false);
        release_request(request);
        release_request(request);
        return ESP_ERR_NO_MEM;
    }
    for (;;) {
        result = allowed(deadline, still_current, context);
        if (result != ESP_OK) break;
        int64_t milliseconds = (deadline - esp_timer_get_time() + 999) / 1000;
        if (milliseconds <= 0) { result = ESP_ERR_TIMEOUT; break; }
        if (milliseconds > 20) milliseconds = 20;
        TickType_t ticks = pdMS_TO_TICKS((uint32_t)milliseconds);
        if (!ticks) ticks = 1;
        if (xSemaphoreTake(request->completed, ticks) == pdTRUE) {
            result = allowed(deadline, still_current, context);
            if (result == ESP_OK) {
                result = request->result;
                if (result == ESP_OK) ip_addr_copy(*address, request->address);
            }
            break;
        }
    }
    /* Timeout/cancel relinquishes only the caller's reference. The callback's
     * semaphore/input stay alive, and it releases the sole query slot later. */
    atomic_store(&request->wanted, false);
    release_request(request);
    return result;
}
