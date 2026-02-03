/**
 * @file ts_mempool.c
 * @brief Memory pool implementation for reducing DRAM fragmentation
 *
 * 使用 PSRAM 预分配内存池，减少 DRAM 碎片。
 * 池中的块大小固定，避免碎片化。
 */

#include "ts_mempool.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ts_mempool";

// 池配置：块大小和数量
typedef struct {
    size_t block_size;
    size_t block_count;
} pool_config_t;

static const pool_config_t POOL_CONFIG[TS_POOL_COUNT] = {
    [TS_POOL_SMALL]  = { .block_size = 256,  .block_count = 16 },  // 4 KB total
    [TS_POOL_MEDIUM] = { .block_size = 1024, .block_count = 12 },  // 12 KB total
    [TS_POOL_LARGE]  = { .block_size = 4096, .block_count = 8 },   // 32 KB total
    [TS_POOL_XLARGE] = { .block_size = 8192, .block_count = 4 },   // 32 KB total
};
// 总计：~80 KB PSRAM

// 池块头部（用于追踪归属）
typedef struct {
    uint32_t magic;          // 魔数验证
    ts_pool_size_t pool_id;  // 所属池
    uint8_t block_idx;       // 块索引
    uint8_t in_use;          // 使用状态
} block_header_t;

#define POOL_MAGIC 0x504F4F4C  // "POOL"
#define HEADER_SIZE sizeof(block_header_t)

// 池结构
typedef struct {
    uint8_t *memory;         // 池内存（含头部）
    uint8_t *bitmap;         // 使用位图
    size_t block_size;       // 实际块大小（含头部）
    size_t block_count;
    size_t used_count;
    size_t peak_usage;
    size_t alloc_count;
    size_t fallback_count;
    SemaphoreHandle_t mutex;
} memory_pool_t;

static memory_pool_t s_pools[TS_POOL_COUNT];
static bool s_initialized = false;

// 选择合适的池
static ts_pool_size_t select_pool(size_t size)
{
    for (int i = 0; i < TS_POOL_COUNT; i++) {
        if (size <= POOL_CONFIG[i].block_size) {
            return (ts_pool_size_t)i;
        }
    }
    return TS_POOL_COUNT;  // 超过最大池大小
}

esp_err_t ts_mempool_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing memory pools in PSRAM...");

    for (int i = 0; i < TS_POOL_COUNT; i++) {
        memory_pool_t *pool = &s_pools[i];
        const pool_config_t *cfg = &POOL_CONFIG[i];

        pool->block_size = cfg->block_size + HEADER_SIZE;
        pool->block_count = cfg->block_count;
        pool->used_count = 0;
        pool->peak_usage = 0;
        pool->alloc_count = 0;
        pool->fallback_count = 0;

        // 分配池内存到 PSRAM
        size_t total_size = pool->block_size * pool->block_count;
        pool->memory = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
        if (!pool->memory) {
            ESP_LOGE(TAG, "Failed to allocate pool %d (%zu bytes)", i, total_size);
            ts_mempool_deinit();
            return ESP_ERR_NO_MEM;
        }

        // 位图也放 PSRAM（很小）
        size_t bitmap_size = (pool->block_count + 7) / 8;
        pool->bitmap = heap_caps_calloc(1, bitmap_size, MALLOC_CAP_SPIRAM);
        if (!pool->bitmap) {
            ESP_LOGE(TAG, "Failed to allocate bitmap for pool %d", i);
            ts_mempool_deinit();
            return ESP_ERR_NO_MEM;
        }

        // 创建互斥锁
        pool->mutex = xSemaphoreCreateMutex();
        if (!pool->mutex) {
            ESP_LOGE(TAG, "Failed to create mutex for pool %d", i);
            ts_mempool_deinit();
            return ESP_ERR_NO_MEM;
        }

        // 初始化所有块头部
        for (size_t j = 0; j < pool->block_count; j++) {
            block_header_t *header = (block_header_t *)(pool->memory + j * pool->block_size);
            header->magic = POOL_MAGIC;
            header->pool_id = (ts_pool_size_t)i;
            header->block_idx = j;
            header->in_use = 0;
        }

        ESP_LOGI(TAG, "Pool %d: %zu x %zu bytes = %zu KB (PSRAM)",
                 i, pool->block_count, cfg->block_size, total_size / 1024);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Memory pools initialized successfully");
    return ESP_OK;
}

void ts_mempool_deinit(void)
{
    for (int i = 0; i < TS_POOL_COUNT; i++) {
        memory_pool_t *pool = &s_pools[i];
        if (pool->memory) {
            heap_caps_free(pool->memory);
            pool->memory = NULL;
        }
        if (pool->bitmap) {
            heap_caps_free(pool->bitmap);
            pool->bitmap = NULL;
        }
        if (pool->mutex) {
            vSemaphoreDelete(pool->mutex);
            pool->mutex = NULL;
        }
    }
    s_initialized = false;
    ESP_LOGI(TAG, "Memory pools deinitialized");
}

void *ts_mempool_alloc(size_t size)
{
    if (!s_initialized) {
        // 未初始化，直接使用 PSRAM
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    ts_pool_size_t pool_id = select_pool(size);
    
    // 超过最大池大小，使用 PSRAM 堆
    if (pool_id >= TS_POOL_COUNT) {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    memory_pool_t *pool = &s_pools[pool_id];

    if (xSemaphoreTake(pool->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        // 超时，fallback 到堆
        pool->fallback_count++;
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    pool->alloc_count++;

    // 查找空闲块
    for (size_t i = 0; i < pool->block_count; i++) {
        size_t byte_idx = i / 8;
        uint8_t bit_mask = 1 << (i % 8);

        if (!(pool->bitmap[byte_idx] & bit_mask)) {
            // 找到空闲块
            pool->bitmap[byte_idx] |= bit_mask;
            pool->used_count++;
            if (pool->used_count > pool->peak_usage) {
                pool->peak_usage = pool->used_count;
            }

            block_header_t *header = (block_header_t *)(pool->memory + i * pool->block_size);
            header->in_use = 1;

            xSemaphoreGive(pool->mutex);

            // 返回用户数据区（跳过头部）
            return (uint8_t *)header + HEADER_SIZE;
        }
    }

    // 池已满，fallback 到 PSRAM 堆
    pool->fallback_count++;
    xSemaphoreGive(pool->mutex);

    ESP_LOGD(TAG, "Pool %d exhausted, fallback to heap", pool_id);
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *ts_mempool_calloc(size_t size)
{
    void *ptr = ts_mempool_alloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void ts_mempool_free(void *ptr)
{
    if (!ptr) {
        return;
    }

    // 检查是否来自池
    block_header_t *header = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);

    if (header->magic == POOL_MAGIC && header->pool_id < TS_POOL_COUNT) {
        // 来自池
        memory_pool_t *pool = &s_pools[header->pool_id];

        if (xSemaphoreTake(pool->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (header->in_use) {
                header->in_use = 0;

                size_t byte_idx = header->block_idx / 8;
                uint8_t bit_mask = 1 << (header->block_idx % 8);
                pool->bitmap[byte_idx] &= ~bit_mask;
                pool->used_count--;
            }
            xSemaphoreGive(pool->mutex);
        }
    } else {
        // 来自堆
        heap_caps_free(ptr);
    }
}

bool ts_mempool_is_pooled(const void *ptr)
{
    if (!ptr) return false;

    block_header_t *header = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    return (header->magic == POOL_MAGIC && header->pool_id < TS_POOL_COUNT);
}

esp_err_t ts_mempool_get_stats(ts_pool_size_t pool_type, ts_pool_stats_t *stats)
{
    if (pool_type >= TS_POOL_COUNT || !stats) {
        return ESP_ERR_INVALID_ARG;
    }

    memory_pool_t *pool = &s_pools[pool_type];
    stats->block_size = POOL_CONFIG[pool_type].block_size;
    stats->total_blocks = pool->block_count;
    stats->used_blocks = pool->used_count;
    stats->peak_usage = pool->peak_usage;
    stats->alloc_count = pool->alloc_count;
    stats->fallback_count = pool->fallback_count;

    return ESP_OK;
}

void ts_mempool_print_stats(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "Memory pools not initialized");
        return;
    }

    ESP_LOGI(TAG, "=== Memory Pool Statistics ===");
    for (int i = 0; i < TS_POOL_COUNT; i++) {
        ts_pool_stats_t stats;
        ts_mempool_get_stats((ts_pool_size_t)i, &stats);

        ESP_LOGI(TAG, "Pool %d (%zu bytes): %zu/%zu used, peak=%zu, allocs=%zu, fallback=%zu",
                 i, stats.block_size, stats.used_blocks, stats.total_blocks,
                 stats.peak_usage, stats.alloc_count, stats.fallback_count);
    }
}
