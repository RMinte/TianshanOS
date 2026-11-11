"""Production OTA server resolution and persisted legacy address migration."""
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
static char www_url[300];
static esp_err_t ts_ota_www_start(const char *url,bool skip,void *callback,void *ctx) {
    assert(!skip); strcpy(www_url,url); return ESP_OK;
}
'''
code += f'#define CONFIG_TS_OTA_DEFAULT_URL "{default}"\n'
source_text = source.read_text()
code += source_text[source_text.index('#define NVS_NAMESPACE_OTA'):source_text.index('static esp_err_t api_ota_status')]
for name in ('ts_api_result_ok', 'ts_api_result_error', 'ts_api_result_free'):
    code += body('components/ts_api/src/ts_api.c', name)
code += body(source, 'api_ota_www_start')
code += r'''
static void check_server(const char *expected) {
    ts_api_result_t result={0};
    assert(api_ota_server_get(NULL,&result)==ESP_OK);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(result.data,"configured")));
    assert(!strcmp(cJSON_GetObjectItem(result.data,"url")->valuestring,expected));
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

    params=cJSON_CreateObject();
    assert(api_ota_www_start(params,&result)==ESP_OK);
    assert(!strcmp(www_url,"https://ota.rminte.com/www.bin"));
    cJSON_Delete(params);
    ts_api_result_free(&result);
    puts("PASS OTA server: default, exact legacy migration, custom URL, clear and WWW fallback");
}
'''
run_native(code, [IDF / 'components/json/cJSON/cJSON.c'])
