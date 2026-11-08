#include "ts_file_store.h"
#include "esp_log.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *store_tag = "file_store";

static esp_err_t store_io_error(void)
{
    return errno == ENOSPC ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
}

static int store_file_state(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return S_ISREG(st.st_mode) ? 1 : -1;
    return errno == ENOENT ? 0 : -1;
}

static bool store_remove(const char *path)
{
    return unlink(path) == 0 || errno == ENOENT;
}

static esp_err_t store_matches(const char *path, const void *bytes, size_t length)
{
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ESP_ERR_NOT_FOUND : store_io_error();
    unsigned char chunk[1024];
    size_t position = 0;
    esp_err_t ret = ESP_OK;
    while (position < length) {
        size_t count = length - position;
        if (count > sizeof(chunk)) count = sizeof(chunk);
        size_t got = fread(chunk, 1, count, file);
        if (got != count || memcmp(chunk, (const unsigned char *)bytes + position, count)) {
            ret = ESP_FAIL;
            break;
        }
        position += count;
    }
    if (ret == ESP_OK && (fgetc(file) != EOF || ferror(file))) ret = ESP_FAIL;
    if (fclose(file) != 0 && ret == ESP_OK) ret = store_io_error();
    return ret;
}

static esp_err_t store_prepare(const char *path, const void *bytes, size_t length, bool *created)
{
    /* Recovery/selection established that the pending name is unused. Never
     * truncate a racing or unrelated file which acquired that name meanwhile. */
    *created = false;
    FILE *file = fopen(path, "wbx");
    if (!file) return store_io_error();
    *created = true;
    esp_err_t ret = !length || fwrite(bytes, 1, length, file) == length ? ESP_OK : store_io_error();
    if (fflush(file) != 0 && ret == ESP_OK) ret = store_io_error();
    if (fsync(fileno(file)) != 0 && ret == ESP_OK) ret = store_io_error();
    if (fclose(file) != 0 && ret == ESP_OK) ret = store_io_error();
    return ret == ESP_OK ? store_matches(path, bytes, length) : ret;
}

esp_err_t ts_file_store_recover(const ts_file_store_paths_t *p, bool writing,
    ts_file_store_validate_t validate, void *context)
{
    int current = store_file_state(p->current), pending = store_file_state(p->pending);
    int previous = store_file_state(p->previous);
    if (current < 0 || pending < 0 || previous < 0) return ESP_ERR_NOT_FINISHED;
    if (pending) {
        if (previous) {
            if (current || rename(p->previous, p->current) != 0) return ESP_ERR_NOT_FINISHED;
        }
        return store_remove(p->pending) ? ESP_OK : ESP_FAIL;
    }
    if (!previous) return ESP_OK;
    if (!current)
        return rename(p->previous, p->current) == 0 ? ESP_OK : ESP_ERR_NOT_FINISHED;
    if (validate && validate(p->current, context) != ESP_OK) return ESP_ERR_NOT_FINISHED;
    if (!store_remove(p->previous)) {
        ESP_LOGW(store_tag, "Committed file retained its previous copy");
        return writing ? ESP_FAIL : ESP_OK;
    }
    return ESP_OK;
}

esp_err_t ts_file_store_replace(const ts_file_store_paths_t *p,
    const void *bytes, size_t length, ts_file_store_mode_t mode,
    ts_file_store_validate_t validate, void *context)
{
    if (!p || !p->current || !p->pending || !p->previous || (!bytes && length))
        return ESP_ERR_INVALID_ARG;
    esp_err_t ret;
    if (mode == TS_FILE_STORE_FIXED) {
        ret = ts_file_store_recover(p, true, validate, context);
    } else if (mode == TS_FILE_STORE_FRESH) {
        ret = store_file_state(p->pending) == 0 && store_file_state(p->previous) == 0
            ? ESP_OK : ESP_ERR_NOT_FINISHED;
    } else return ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK) return ret;
    int current = store_file_state(p->current);
    if (current < 0) return ESP_ERR_NOT_FINISHED;
    bool created;
    ret = store_prepare(p->pending, bytes, length, &created);
    if (ret != ESP_OK) {
        if (!created) return ret; /* Never clean up a failed exclusive open. */
        goto uncommitted;
    }
    if (current && rename(p->current, p->previous) != 0) {
        ret = store_io_error();
        goto uncommitted;
    }
    if (rename(p->pending, p->current) != 0) {
        ret = store_io_error();
        /* A driver may report an error after taking effect. Compare actual
         * installed bytes before reporting failure or attempting rollback. */
        int pending = store_file_state(p->pending);
        esp_err_t observed = store_matches(p->current, bytes, length);
        if (!(pending == 0 && observed == ESP_OK)) {
            if (pending != 1 || (observed != ESP_ERR_NOT_FOUND && observed != ESP_OK))
                return ESP_ERR_NOT_FINISHED;
            goto uncommitted;
        }
    }
    if (current && !store_remove(p->previous))
        ESP_LOGW(store_tag, "Committed file retained its previous copy");
    return ESP_OK;
uncommitted:
    return ts_file_store_recover(p, false, validate, context) == ESP_OK ? ret : ESP_ERR_NOT_FINISHED;
}
