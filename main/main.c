/**
 * @file main.c
 * @brief TianShanOS Main Entry Point
 *
 * TianShanOS 主程序入口
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "ts_core.h"

static const char *TAG = "main";

/**
 * @brief 打印启动横幅
 */
static void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                                                           ║\n");
    printf("║   ████████╗██╗ █████╗ ███╗   ██╗███████╗██╗  ██╗ █████╗   ║\n");
    printf("║   ╚══██╔══╝██║██╔══██╗████╗  ██║██╔════╝██║  ██║██╔══██╗  ║\n");
    printf("║      ██║   ██║███████║██╔██╗ ██║███████╗███████║███████║  ║\n");
    printf("║      ██║   ██║██╔══██║██║╚██╗██║╚════██║██╔══██║██╔══██║  ║\n");
    printf("║      ██║   ██║██║  ██║██║ ╚████║███████║██║  ██║██║  ██║  ║\n");
    printf("║      ╚═╝   ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝  ║\n");
    printf("║                                                           ║\n");
    printf("║                 TianShanOS v%s                       ║\n", TIANSHAN_OS_VERSION_STRING);
    printf("║           ESP32 Rack Management Operating System          ║\n");
    printf("║                                                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * @brief 打印系统信息
 */
static void print_system_info(void)
{
    ESP_LOGI(TAG, "System Information:");
    ESP_LOGI(TAG, "  - IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "  - Free Heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  - Min Free Heap: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "  - Chip: %s, %d cores, WiFi%s%s",
             (chip_info.model == CHIP_ESP32S3) ? "ESP32-S3" :
             (chip_info.model == CHIP_ESP32) ? "ESP32" : "Unknown",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
}

/**
 * @brief 应用程序主入口
 */
void app_main(void)
{
    esp_err_t ret;

    // 打印启动横幅
    print_banner();

    // 打印系统信息
    print_system_info();

    // 初始化 TianShanOS 核心
    ESP_LOGI(TAG, "Initializing TianShanOS...");
    ret = ts_core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TianShanOS core: %s", esp_err_to_name(ret));
        return;
    }

    // 启动所有服务
    ESP_LOGI(TAG, "Starting TianShanOS services...");
    ret = ts_core_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TianShanOS: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "TianShanOS started successfully!");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    // 打印服务状态
    ts_service_dump();

    // 主循环
    ESP_LOGI(TAG, "Entering main loop...");

    while (1) {
        // 定期打印堆信息（调试用）
        vTaskDelay(pdMS_TO_TICKS(60000));  // 每分钟

#ifdef CONFIG_TS_LOG_DEBUG
        ESP_LOGI(TAG, "Heap: free=%lu, min=%lu",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
#endif
    }
}
