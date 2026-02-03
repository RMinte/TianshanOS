/**
 * @file ts_mempool.h
 * @brief Memory pool for reducing DRAM fragmentation
 *
 * Provides pre-allocated buffer pools for common allocation sizes,
 * reducing malloc/free calls and DRAM fragmentation.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Predefined pool sizes
 */
typedef enum {
    TS_POOL_SMALL = 0,    /**< 256 bytes */
    TS_POOL_MEDIUM,       /**< 1024 bytes */
    TS_POOL_LARGE,        /**< 4096 bytes */
    TS_POOL_XLARGE,       /**< 8192 bytes */
    TS_POOL_COUNT
} ts_pool_size_t;

/**
 * @brief Pool statistics
 */
typedef struct {
    size_t block_size;      /**< Size of each block */
    size_t total_blocks;    /**< Total blocks in pool */
    size_t used_blocks;     /**< Currently allocated blocks */
    size_t peak_usage;      /**< Peak usage (high water mark) */
    size_t alloc_count;     /**< Total allocation requests */
    size_t fallback_count;  /**< Allocations that fell back to heap */
} ts_pool_stats_t;

/**
 * @brief Initialize memory pool system
 *
 * Allocates pools in PSRAM to reduce DRAM fragmentation.
 * Should be called early in boot process.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_mempool_init(void);

/**
 * @brief Deinitialize memory pool system
 */
void ts_mempool_deinit(void);

/**
 * @brief Allocate buffer from pool
 *
 * Automatically selects appropriate pool based on size.
 * Falls back to heap_caps_malloc(PSRAM) if pool exhausted.
 *
 * @param size Required size in bytes
 * @return Pointer to buffer, or NULL on failure
 */
void *ts_mempool_alloc(size_t size);

/**
 * @brief Free buffer back to pool
 *
 * Automatically detects if buffer is from pool or heap.
 *
 * @param ptr Pointer to buffer (can be NULL)
 */
void ts_mempool_free(void *ptr);

/**
 * @brief Allocate zeroed buffer from pool
 *
 * @param size Required size in bytes
 * @return Pointer to zeroed buffer, or NULL on failure
 */
void *ts_mempool_calloc(size_t size);

/**
 * @brief Get pool statistics
 *
 * @param pool_type Pool type to query
 * @param stats Output statistics structure
 * @return ESP_OK on success
 */
esp_err_t ts_mempool_get_stats(ts_pool_size_t pool_type, ts_pool_stats_t *stats);

/**
 * @brief Print pool statistics to log
 */
void ts_mempool_print_stats(void);

/**
 * @brief Check if pointer belongs to pool
 *
 * @param ptr Pointer to check
 * @return true if from pool, false if from heap
 */
bool ts_mempool_is_pooled(const void *ptr);

#ifdef __cplusplus
}
#endif
