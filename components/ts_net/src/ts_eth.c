/**
 * @file ts_eth.c
 * @brief Ethernet Driver (W5500) Implementation
 */

#include "ts_eth.h"
#include "ts_net.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_mac_spi.h"
#include "esp_event.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

#define TAG "ts_eth"

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static bool s_link_up = false;
static bool s_initialized = false;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            TS_LOGI(TAG, "Ethernet link up");
            s_link_up = true;
            ts_event_post(TS_EVENT_NETWORK, TS_EVT_ETH_CONNECTED, NULL, 0, 0);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            TS_LOGI(TAG, "Ethernet link down");
            s_link_up = false;
            ts_event_post(TS_EVENT_NETWORK, TS_EVT_ETH_DISCONNECTED, NULL, 0, 0);
            break;
        case ETHERNET_EVENT_START:
            TS_LOGI(TAG, "Ethernet started");
            break;
        case ETHERNET_EVENT_STOP:
            TS_LOGI(TAG, "Ethernet stopped");
            break;
    }
}

static void got_ip_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (event->esp_netif == s_eth_netif) {
        TS_LOGI(TAG, "Ethernet got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ts_event_post(TS_EVENT_NETWORK, TS_EVT_GOT_IP, 
                      &event->ip_info, sizeof(event->ip_info), 0);
    }
}

esp_err_t ts_eth_init(const ts_eth_config_t *config)
{
    if (s_initialized) return ESP_OK;
    if (!config) return ESP_ERR_INVALID_ARG;
    
    TS_LOGI(TAG, "Initializing Ethernet (W5500)");
    
    // Register event handlers
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_handler, NULL);
    
    // Create netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    
    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = config->gpio_mosi,
        .miso_io_num = config->gpio_miso,
        .sclk_io_num = config->gpio_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t ret = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        TS_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // W5500 SPI device
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = config->spi_clock_mhz * 1000 * 1000,
        .spics_io_num = config->gpio_cs,
        .queue_size = 20,
    };
    
    spi_device_handle_t spi_handle;
    ret = spi_bus_add_device(config->spi_host, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "SPI device add failed");
        return ret;
    }
    
    // W5500 MAC driver
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    w5500_config.int_gpio_num = config->gpio_int;
    
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (!mac) {
        TS_LOGE(TAG, "Failed to create W5500 MAC");
        return ESP_FAIL;
    }
    
    // W5500 PHY driver
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = config->gpio_rst;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (!phy) {
        TS_LOGE(TAG, "Failed to create W5500 PHY");
        return ESP_FAIL;
    }
    
    // Create Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Ethernet driver install failed");
        return ret;
    }
    
    // Attach to netif
    esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle));
    
    s_initialized = true;
    TS_LOGI(TAG, "Ethernet initialized");
    return ESP_OK;
}

esp_err_t ts_eth_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    ts_eth_stop();
    
    if (s_eth_handle) {
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    
    if (s_eth_netif) {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_eth_start(void)
{
    if (!s_eth_handle) return ESP_ERR_INVALID_STATE;
    return esp_eth_start(s_eth_handle);
}

esp_err_t ts_eth_stop(void)
{
    if (!s_eth_handle) return ESP_ERR_INVALID_STATE;
    return esp_eth_stop(s_eth_handle);
}

bool ts_eth_is_link_up(void)
{
    return s_link_up;
}

esp_netif_t *ts_eth_get_netif(void)
{
    return s_eth_netif;
}
