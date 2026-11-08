#include "ts_storage.h"
#include "ts_config_partitions.h"
#include <string.h>
#include <strings.h>

/* FatFs resolves its own short names and Unicode case equivalents. */
extern esp_err_t ts_storage_sd_resolve_name(const char *path, char *name, size_t capacity);

static esp_err_t directory_is(const char *path, const char *expected, bool *matches)
{
    const char *name = strrchr(expected, '/') + 1;
    const char *component = strrchr(path, '/') + 1;
    if (!strcasecmp(component, name)) { *matches = true; return ESP_OK; }
    char resolved[TS_STORAGE_MAX_PATH];
    esp_err_t ret = ts_storage_sd_resolve_name(path, resolved, sizeof(resolved));
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) return ret;
    *matches = ret == ESP_OK && !strcasecmp(resolved, name);
    return ESP_OK;
}

static esp_err_t normalize_sd_suffix(const char *at, char normalized[TS_STORAGE_MAX_PATH])
{
    /* Normalize within the VFS mount. Both separators and trailing dots/spaces
     * have FAT semantics; '..' cannot change which VFS mount owns this path. */
    strcpy(normalized, "/sdcard");
    size_t used = 7;
    const char *end;
    while (*at) {
        while (*at == '/' || *at == '\\') ++at;
        if (!*at) break;
        end = at;
        while (*end && *end != '/' && *end != '\\') ++end;
        size_t length = (size_t)(end - at);
        while (length && at[length - 1] == ' ') --length;
        if (length == 2 && !strncmp(at, "..", 2)) {
            if (used > 7) {
                used = (size_t)(strrchr(normalized, '/') - normalized);
                normalized[used] = 0;
            }
        } else if (length != 1 || at[0] != '.') {
            while (length && (at[length - 1] == '.' || at[length - 1] == ' ')) --length;
            if (!length) return ESP_ERR_INVALID_ARG;
            normalized[used++] = '/';
            memcpy(normalized + used, at, length);
            used += length;
            normalized[used] = 0;
        }
        at = end;
    }
    return ESP_OK;
}

esp_err_t ts_storage_check_write_path(const char *path, bool include_children)
{
    if (!path || path[0] != '/' || strlen(path) >= TS_STORAGE_MAX_PATH) return ESP_ERR_INVALID_ARG;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        /* FatFs treats a control character as the end of the path. */
        if (*p < ' ' || *p == 0x7f) return ESP_ERR_INVALID_ARG;
    }
    const char *at = path;
    while (*at == '/') ++at;
    const char *end = strchr(at, '/');
    size_t mount = end ? (size_t)(end - at) : strlen(at);
    if (!mount) return include_children ? ESP_ERR_NOT_ALLOWED : ESP_OK;
    if (mount == strlen(&TS_CONFIG_AUTO_MOUNT[1]) && !strncmp(at, &TS_CONFIG_AUTO_MOUNT[1], mount)) {
        return ESP_ERR_NOT_ALLOWED;
    }
    if (mount != 6 || strncmp(at, "sdcard", 6)) return ESP_OK;

    char normalized[TS_STORAGE_MAX_PATH];
    esp_err_t ret = normalize_sd_suffix(end ? end : at + mount, normalized);
    if (ret != ESP_OK) return ret;
    size_t used = strlen(normalized);
    if (used == 7) return include_children ? ESP_ERR_NOT_ALLOWED : ESP_OK;
    /* Preserve the existing automation configuration owner through the
     * same FAT case/short-name owner check, including recursive parent removal. */
    const char *owned[][2] = {
        {TS_CONFIG_AUTO_SD_PARENT, TS_CONFIG_AUTO_SD_DIR},
    };
    char *parent_end = strchr(normalized + 8, '/');
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); ++i) {
        if (parent_end) *parent_end = 0;
        bool parent_matches;
        ret = directory_is(normalized, owned[i][0], &parent_matches);
        if (parent_end) *parent_end = '/';
        if (ret != ESP_OK) return ret;
        if (!parent_matches) continue;
        if (!parent_end) return include_children ? ESP_ERR_NOT_ALLOWED : ESP_OK;
        char *directory_end = strchr(parent_end + 1, '/');
        if (directory_end) *directory_end = 0;
        bool directory_matches;
        ret = directory_is(normalized, owned[i][1], &directory_matches);
        if (directory_end) *directory_end = '/';
        if (ret != ESP_OK) return ret;
        if (directory_matches) return ESP_ERR_NOT_ALLOWED;
    }
    return ESP_OK;
}

/* VFS selects the mount before FAT interprets its path. Do not turn the
 * internal owner guard into a global SD-only restriction. */
esp_err_t ts_storage_check_external_write_path(const char *path, bool include_children)
{
    if (!path || !((!strncmp(path, "/sdcard/", 8) && path[8]) ||
                   (!strncmp(path, "/spiffs/", 8) && path[8]) ||
                   (!strncmp(path, "/www/", 5) && path[5]))) return ESP_ERR_INVALID_ARG;
    return ts_storage_check_write_path(path, include_children);
}

/* Private roots are checked after FAT normalization/resolution. Metadata errors
 * fail closed: a failed alias lookup never turns a private path into SD data. */
esp_err_t ts_storage_external_read_requires_admin(const char *path, bool *requires_admin)
{
    if (!requires_admin) return ESP_ERR_INVALID_ARG;
    *requires_admin = true;
    if (!path || path[0] != '/' || strlen(path) >= TS_STORAGE_MAX_PATH) return ESP_ERR_INVALID_ARG;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        if (*p < ' ' || *p == 0x7f) return ESP_ERR_INVALID_ARG;
    }
    const char *private_mounts[] = {"/spiffs", "/www", TS_CONFIG_AUTO_MOUNT};
    for (size_t i = 0; i < sizeof(private_mounts) / sizeof(private_mounts[0]); ++i) {
        size_t length = strlen(private_mounts[i]);
        if (!strncmp(path, private_mounts[i], length) && (!path[length] || path[length] == '/')) return ESP_OK;
    }
    if (strncmp(path, "/sdcard", 7) || (path[7] && path[7] != '/')) return ESP_ERR_INVALID_ARG;
    char normalized[TS_STORAGE_MAX_PATH];
    esp_err_t ret = normalize_sd_suffix(path + 7, normalized);
    if (ret != ESP_OK) return ret;
    if (!strcmp(normalized, "/sdcard")) { *requires_admin = false; return ESP_OK; }
    char *end = strchr(normalized + 8, '/');
    if (end) *end = 0;
    const char *name = normalized + 8;
    if (!strcasecmp(name, "config") || !strcasecmp(name, "pki")) return ESP_OK;
    char resolved[TS_STORAGE_MAX_PATH];
    ret = ts_storage_sd_resolve_name(normalized, resolved, sizeof(resolved));
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) return ret;
    *requires_admin = ret == ESP_OK && (!strcasecmp(resolved, "config") || !strcasecmp(resolved, "pki"));
    return ESP_OK;
}
