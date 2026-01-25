/**
 * @file ts_net.h
 * @brief TianShanOS Network API
 */

#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Network interface types */
typedef enum {
    TS_NET_IF_ETH = 0,
    TS_NET_IF_WIFI_STA,
    TS_NET_IF_WIFI_AP,
    TS_NET_IF_MAX
} ts_net_if_t;

/** Network status */
typedef enum {
    TS_NET_STATUS_DOWN,
    TS_NET_STATUS_CONNECTING,
    TS_NET_STATUS_CONNECTED,
    TS_NET_STATUS_ERROR
} ts_net_status_t;

/** IP configuration */
typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1;
    uint32_t dns2;
} ts_net_ip_info_t;

/** Interface statistics */
typedef struct {
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
} ts_net_stats_t;

/**
 * @brief Initialize network subsystem
 */
esp_err_t ts_net_init(void);

/**
 * @brief Initialize mDNS service (call after obtaining IP)
 * @note This is automatically called when IP is obtained, but can be called manually
 */
esp_err_t ts_net_mdns_start(void);

/**
 * @brief Deinitialize network subsystem
 */
esp_err_t ts_net_deinit(void);

/**
 * @brief Get interface status
 */
ts_net_status_t ts_net_get_status(ts_net_if_t iface);

/**
 * @brief Get IP info for interface
 */
esp_err_t ts_net_get_ip_info(ts_net_if_t iface, ts_net_ip_info_t *info);

/**
 * @brief Set static IP for interface
 */
esp_err_t ts_net_set_ip_info(ts_net_if_t iface, const ts_net_ip_info_t *info);

/**
 * @brief Enable DHCP client
 */
esp_err_t ts_net_enable_dhcp(ts_net_if_t iface);

/**
 * @brief Get interface statistics
 */
esp_err_t ts_net_get_stats(ts_net_if_t iface, ts_net_stats_t *stats);

/**
 * @brief Get MAC address
 */
esp_err_t ts_net_get_mac(ts_net_if_t iface, uint8_t mac[6]);

/**
 * @brief Set hostname
 */
esp_err_t ts_net_set_hostname(const char *hostname);

/**
 * @brief Get hostname
 */
const char *ts_net_get_hostname(void);

/**
 * @brief Convert IP to string
 */
const char *ts_net_ip_to_str(uint32_t ip);

/**
 * @brief Parse IP from string
 */
esp_err_t ts_net_str_to_ip(const char *str, uint32_t *ip);

#ifdef __cplusplus
}
#endif
