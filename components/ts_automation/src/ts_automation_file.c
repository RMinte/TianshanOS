#include "ts_automation_file.h"
#include "ts_file_store.h"
#include "ts_config_partitions.h"
#include "ts_crypto.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_BYTES 0x200000U

typedef ts_file_store_paths_t paths_t;

static const paths_t s_paths[] = {
    {TS_CONFIG_AUTO_SD_DIR "/state.json", TS_CONFIG_AUTO_SD_DIR "/state.pending",
     TS_CONFIG_AUTO_SD_DIR "/state.previous"},
    {TS_CONFIG_AUTO_MOUNT "/state.json", TS_CONFIG_AUTO_MOUNT "/state.pending",
     TS_CONFIG_AUTO_MOUNT "/state.previous"}
};

static esp_err_t io_error(void)
{
    return errno == ENOSPC ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
}

static bool valid_document(const char *text, size_t size)
{
    cJSON *json = cJSON_ParseWithLengthOpts(text, size + 1, NULL, true);
    /* Business/schema checks belong to the configuration owner. A readable
     * object must remain available to an explicit pause-and-force-delete even
     * when some definitions or metadata are invalid. */
    bool valid = cJSON_IsObject(json);
    cJSON_Delete(json);
    return valid;
}

static esp_err_t read_bytes(const char *path, char **out, size_t *length)
{
    *out = NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ESP_ERR_NOT_FOUND : io_error();
    esp_err_t ret = ESP_OK;
    if (fseek(file, 0, SEEK_END) != 0) ret = io_error();
    long size = ret == ESP_OK ? ftell(file) : -1;
    if (ret == ESP_OK && size < 0) ret = io_error();
    if (ret == ESP_OK && fseek(file, 0, SEEK_SET) != 0) ret = io_error();
    char *text = ret == ESP_OK ?
        heap_caps_malloc((size_t)size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (ret == ESP_OK && !text) ret = ESP_ERR_NO_MEM;
    if (ret == ESP_OK) {
        if (fread(text, 1, size, file) != (size_t)size || ferror(file)) ret = io_error();
        else {
            text[size] = 0;
        }
    }
    if (fclose(file) != 0 && ret == ESP_OK) ret = io_error();
    if (ret == ESP_OK) { *out = text;if(length)*length=size; }
    else free(text);
    return ret;
}

static esp_err_t read_file(const char *path, char **out)
{
    size_t length=0;
    esp_err_t ret=read_bytes(path,out,&length);
    if(ret==ESP_OK && !valid_document(*out,length)){
        free(*out);*out=NULL;ret=ESP_ERR_INVALID_ARG;
    }
    return ret;
}

static esp_err_t validate_file(const char *path, void *context)
{
    (void)context;
    char *text = NULL;
    esp_err_t ret = read_file(path, &text);
    free(text);
    return ret;
}

static esp_err_t ensure_sd_directory(void)
{
    const char *paths[] = {TS_CONFIG_AUTO_SD_PARENT, TS_CONFIG_AUTO_SD_DIR};
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (mkdir(paths[i], 0775) == 0) continue;
        if (errno != EEXIST) return io_error();
        struct stat st;
        if (stat(paths[i], &st) != 0 || !S_ISDIR(st.st_mode)) return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ts_auto_file_load(ts_auto_file_location_t location, char **json)
{
    if (!json || (unsigned)location >= sizeof(s_paths) / sizeof(s_paths[0]))
        return ESP_ERR_INVALID_ARG;
    *json = NULL;
    const paths_t *p = &s_paths[location];
    esp_err_t ret = ts_file_store_recover(p, false, validate_file, NULL);
    return ret == ESP_OK ? read_file(p->current, json) : ret;
}

esp_err_t ts_auto_file_fingerprint(ts_auto_file_location_t location,char digest[65])
{
    if(!digest || (unsigned)location>=sizeof(s_paths)/sizeof(s_paths[0]))return ESP_ERR_INVALID_ARG;
    digest[0]=0;
    const paths_t *paths=&s_paths[location];
    esp_err_t ret=ts_file_store_recover(paths, false, validate_file, NULL);
    char *bytes=NULL;size_t length=0;unsigned char hash[32];
    if(ret==ESP_OK)ret=read_bytes(paths->current,&bytes,&length);
    if(ret==ESP_OK)ret=ts_crypto_hash(TS_HASH_SHA256,bytes,length,hash,sizeof(hash));
    if(ret==ESP_OK)ret=ts_crypto_hex_encode(hash,sizeof(hash),digest,65);
    free(bytes);return ret;
}

esp_err_t ts_auto_file_save(ts_auto_file_location_t location, const char *json)
{
    if ((unsigned)location >= sizeof(s_paths) / sizeof(s_paths[0]) || !json)
        return ESP_ERR_INVALID_ARG;
    size_t size = strlen(json);
    if (location == TS_AUTO_FILE_INTERNAL && size > MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    if (!valid_document(json, size)) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = location == TS_AUTO_FILE_SD ? ensure_sd_directory() : ESP_OK;
    const paths_t *p = &s_paths[location];
    if (ret != ESP_OK) return ret;
    return ts_file_store_replace(p, json, size, TS_FILE_STORE_FIXED, validate_file, NULL);
}
