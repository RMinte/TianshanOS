#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/* Trusted C file owners only. Callers hold their writer/mount gate and own
 * all three paths on the same filesystem. No JSON caller supplies these paths.
 * FAT/SPIFFS rename does not overwrite an existing destination. */
typedef struct {
    const char *current, *pending, *previous;
} ts_file_store_paths_t;
typedef esp_err_t (*ts_file_store_validate_t)(const char *path, void *context);

typedef enum {
    TS_FILE_STORE_FIXED,  /* Owner's fixed paths: recover an interrupted save. */
    TS_FILE_STORE_FRESH   /* New unused sidecars: never delete preexisting data. */
} ts_file_store_mode_t;

/* Recovery restores an old file instead of publishing an uncommitted pending
 * file. NOT_FINISHED preserves uncertain evidence. Filesystem power-loss
 * atomicity is not promised by this protocol. */
esp_err_t ts_file_store_recover(const ts_file_store_paths_t *paths, bool writing,
    ts_file_store_validate_t validate, void *context);
/* Complete bytes are flushed, synced, closed and read back before promotion.
 * OK means the new target was observed installed, even if old cleanup failed.
 * NOT_FINISHED means inspect the retained paths before retrying. */
esp_err_t ts_file_store_replace(const ts_file_store_paths_t *paths,
    const void *bytes, size_t length, ts_file_store_mode_t mode,
    ts_file_store_validate_t validate, void *context);
