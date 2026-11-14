"""Production OTA server migration and WWW URL resolution through downloader startup."""
from pathlib import Path
import re
from test_console_lifecycle import IDF, body, run_native

source = Path('components/ts_api/src/ts_api_ota.c')
config = Path('sdkconfig').read_text()
default = re.search(r'^CONFIG_TS_OTA_DEFAULT_URL="([^"]+)"$', config, re.M)[1]
assert default == 'https://ota.rminte.com'
assert f'default "{default}"' in Path('components/ts_ota/Kconfig').read_text()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_api.h"
#include "ts_http_url.h"
#include "ts_ota.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define TS_STRDUP_PSRAM strdup
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static char stored[256];
static bool fail_commit;
static unsigned erases, commits;
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name,"ota_config"));
    if (mode==NVS_READONLY && !stored[0]) return ESP_ERR_NVS_NOT_FOUND;
    *handle=mode;
    return ESP_OK;
}
static esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *len) {
    assert(h==NVS_READONLY && !strcmp(key,"server_url") && *len>strlen(stored));
    strcpy(out,stored); return ESP_OK;
}
static esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *url) {
    assert(h==NVS_READWRITE && !strcmp(key,"server_url"));
    strcpy(stored,url); return ESP_OK;
}
static esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==NVS_READWRITE && !strcmp(key,"server_url"));
    erases++;
    if (!fail_commit) stored[0]=0;
    return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h) { commits++; return fail_commit ? ESP_FAIL : ESP_OK; }
static void nvs_close(nvs_handle_t h) {}
typedef int BaseType_t;
static bool s_www_ota_running, s_www_ota_abort_requested;
static bool s_www_progress_mutex = true;
static unsigned www_task_starts;
#define pdPASS 1
/* Capture task creation without running the downloader. */
#define xTaskCreate(...) (++www_task_starts, pdPASS)
esp_err_t ts_ota_www_init(void) { return ESP_OK; }
'''
www_source = Path('components/ts_ota/src/ts_ota_www.c')
www_text = www_source.read_text()
code += www_text[www_text.index('// Configuration for current www OTA'):
                 www_text.index('// WWW OTA progress tracking')]
code += body(www_source, 'ts_ota_www_start')
code += f'#define CONFIG_TS_OTA_DEFAULT_URL "{default}"\n'
source_text = source.read_text()
code += source_text[source_text.index('#define NVS_NAMESPACE_OTA'):source_text.index('static esp_err_t api_ota_status')]
for name in ('ts_api_result_ok', 'ts_api_result_error', 'ts_api_result_free'):
    code += body('components/ts_api/src/ts_api.c', name)
code += r'''
static bool change_server_during_parse;
static bool parse_www_url(const char *url, struct http_parser_url *parsed) {
    bool valid = ts_http_url_parse(url, parsed);
    if (change_server_during_parse) {
        strcpy(s_ota_server_url, "https://other.fixture.invalid");
        change_server_during_parse = false;
    }
    return valid;
}
#define ts_http_url_parse parse_www_url
'''
code += body(source, 'api_ota_www_start') + '#undef ts_http_url_parse\n'
code += r'''
static void check_server(const char *expected) {
    ts_api_result_t result={0};
    assert(api_ota_server_get(NULL,&result)==ESP_OK);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(result.data,"configured")));
    assert(!strcmp(cJSON_GetObjectItem(result.data,"url")->valuestring,expected));
    ts_api_result_free(&result);
}

static void check_www(const char *explicit_url, const char *expected) {
    ts_api_result_t result = {0};
    cJSON *params = cJSON_CreateObject();
    assert(params);
    if (explicit_url) assert(cJSON_AddStringToObject(params, "url", explicit_url));
    s_www_config.url[0] = '\0';
    s_www_ota_running = false;
    unsigned previous_starts = www_task_starts;
    esp_err_t ret = api_ota_www_start(params, &result);
    if (expected) {
        assert(ret == ESP_OK && result.code == TS_API_OK);
        assert(www_task_starts == previous_starts + 1);
        if (strcmp(s_www_config.url, expected) != 0) {
            fprintf(stderr, "WWW OTA URL: expected %s, got %s\n", expected, s_www_config.url);
            abort();
        }
        assert(!strcmp(cJSON_GetObjectItem(result.data, "url")->valuestring, expected));
    } else {
        assert(ret == ESP_ERR_INVALID_ARG && result.code == TS_API_ERR_INVALID_ARG);
        assert(www_task_starts == previous_starts && !s_www_config.url[0]);
    }
    cJSON_Delete(params);
    ts_api_result_free(&result);
}

int main(void) {
    init_ota_server_config();
    check_server("https://ota.rminte.com");
    assert(!erases && !commits);

    strcpy(stored,"http://a6000nvlink.aoseo.com:15207/");
    init_ota_server_config();
    check_server("https://ota.rminte.com");
    assert(!stored[0] && erases==1 && commits==1);
    init_ota_server_config();
    assert(erases==1 && commits==1);

    strcpy(stored,"http://a6000nvlink.aoseo.com:15207");
    fail_commit=true;
    init_ota_server_config();
    check_server("https://ota.rminte.com");
    assert(stored[0] && erases==2);
    fail_commit=false;

    strcpy(stored,"https://local.fixture.invalid/ota");
    init_ota_server_config();
    check_server(stored);
    assert(erases==2);

    ts_api_result_t result={0};
    cJSON *params=cJSON_Parse("{\"url\":\"\",\"save\":true}");
    assert(api_ota_server_set(params,&result)==ESP_OK && !stored[0]);
    assert(!strcmp(cJSON_GetObjectItem(result.data,"url")->valuestring,"https://ota.rminte.com"));
    cJSON_Delete(params);
    ts_api_result_free(&result);

    check_www(NULL, "https://ota.rminte.com/www.bin");

    static const struct { const char *server; const char *expected; } cases[] = {
        {"https://local.fixture.invalid/update/TianshanOS.bin", "https://local.fixture.invalid/update/www.bin"},
        {"https://local.fixture.invalid/update/TianShanOS.bin", "https://local.fixture.invalid/update/www.bin"},
        {"https://local.fixture.invalid/update/firmware.bin", "https://local.fixture.invalid/update/www.bin"},
        {"https://local.fixture.invalid", "https://local.fixture.invalid/www.bin"},
        {"https://local.fixture.invalid/", "https://local.fixture.invalid/www.bin"},
        {"https://local.fixture.invalid/firmware.bin/channel", "https://local.fixture.invalid/firmware.bin/channel/www.bin"},
        {"https://local.fixture.invalid/update/TianShanOS.bin?download=1", "https://local.fixture.invalid/update/www.bin"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        strcpy(stored, cases[i].server);
        init_ota_server_config();
        check_www(NULL, cases[i].expected);
    }

    const char *explicit_url = "https://other.fixture.invalid/web/custom.bin?source=TianshanOS.bin";
    check_www(explicit_url, explicit_url);

    strcpy(stored, "https://local.fixture.invalid/update/TianshanOS.bin");
    init_ota_server_config();
    change_server_during_parse = true;
    check_www(NULL, "https://local.fixture.invalid/update/www.bin");
    check_www(NULL, "https://other.fixture.invalid/www.bin");

    char boundary_url[257] = "https://local.fixture.invalid/";
    const size_t prefix_len = strlen(boundary_url);
    const size_t max_root_len = 255 - strlen("/www.bin");
    memset(boundary_url + prefix_len, 'a', max_root_len + 1 - prefix_len);
    boundary_url[max_root_len] = '\0';
    strcpy(stored, boundary_url);
    init_ota_server_config();
    strcpy(boundary_url + max_root_len, "/www.bin");
    assert(strlen(boundary_url) == 255);
    check_www(NULL, boundary_url);
    check_www(boundary_url, boundary_url);

    boundary_url[max_root_len] = 'a';
    boundary_url[max_root_len + 1] = '\0';
    strcpy(stored, boundary_url);
    init_ota_server_config();
    strcpy(boundary_url + max_root_len + 1, "/www.bin");
    assert(strlen(boundary_url) == 256);
    check_www(NULL, NULL);
    check_www(boundary_url, NULL);
    unsigned previous_starts = www_task_starts;
    assert(ts_ota_www_start(boundary_url, false, NULL, NULL) == ESP_ERR_INVALID_ARG);
    assert(www_task_starts == previous_starts && !s_www_ota_running && !s_www_config.url[0]);

    /* A 255-byte firmware URL can still be replaced with its shorter companion URL. */
    memset(boundary_url + prefix_len, 'a', 255 - prefix_len);
    strcpy(boundary_url + 255 - strlen("/TianshanOS.bin"), "/TianshanOS.bin");
    strcpy(stored, boundary_url);
    init_ota_server_config();
    strcpy(boundary_url + 255 - strlen("/TianshanOS.bin"), "/www.bin");
    check_www(NULL, boundary_url);
    puts("PASS OTA server: defaults, migration and WWW download URL resolution");
}
'''
http_parser = IDF / 'components/http_parser'
run_native(code, [
    IDF / 'components/json/cJSON/cJSON.c',
    http_parser / 'http_parser.c',
    Path('components/ts_http_fetch/src/ts_http_url.c'),
], extra_include_dirs=[
    http_parser, Path('components/ts_http_fetch/include'), Path('components/ts_ota/include'),
    Path('components/ts_core/ts_event/include'),
])
