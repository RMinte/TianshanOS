/**
 * @file ts_dhcp_server.c
 * @brief DHCP Server (placeholder - using lwIP DHCP server)
 */

#include "ts_net.h"
#include "ts_log.h"
#include "esp_netif.h"

#define TAG "ts_dhcp"

// ESP-IDF provides built-in DHCP server for AP mode via esp_netif
// This file provides additional configuration helpers if needed

esp_err_t ts_dhcp_server_configure(esp_netif_t *netif, uint32_t start_ip, 
                                    uint32_t end_ip, uint32_t lease_time_min)
{
    if (!netif) return ESP_ERR_INVALID_ARG;
    
    // Stop DHCP server first
    esp_netif_dhcps_stop(netif);
    
    // Configure lease time
    esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, 
                           ESP_NETIF_IP_ADDRESS_LEASE_TIME, 
                           &lease_time_min, sizeof(lease_time_min));
    
    // Start DHCP server
    return esp_netif_dhcps_start(netif);
}

esp_err_t ts_dhcp_server_start(esp_netif_t *netif)
{
    if (!netif) return ESP_ERR_INVALID_ARG;
    return esp_netif_dhcps_start(netif);
}

esp_err_t ts_dhcp_server_stop(esp_netif_t *netif)
{
    if (!netif) return ESP_ERR_INVALID_ARG;
    return esp_netif_dhcps_stop(netif);
}
