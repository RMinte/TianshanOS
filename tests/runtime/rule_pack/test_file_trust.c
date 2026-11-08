/* Reuse the existing disposable PKI and storage boundary, not a second crypto fixture. */
#include <unistd.h>
#define main full_pack_crypto_suite
#include "test_crypto.c"
#undef main
int main(int argc,char **argv)
{
    assert(argc==2);const char *dir=argv[1];
    device_key=read_file(dir,"recipient.key");device_cert=read_file(dir,"recipient.pem");ca=read_file(dir,"root.pem");
    assert(ts_config_pack_init()==ESP_OK);
    char *recipient_key=device_key,*recipient_cert=device_cert;
    device_key=read_file(dir,"alternate-developer.key");device_cert=read_file(dir,"alternate-developer.pem");
    ts_config_pack_export_opts_t opts={.recipient_cert_pem=recipient_cert,.recipient_cert_len=strlen(recipient_cert)+1};
    char *bytes=NULL;size_t length=0;const char *content="{\"type\":\"automation_rule\",\"rule\":{\"id\":\"night\",\"name\":\"Night\",\"enabled\":true,\"actions\":[{\"type\":\"log\",\"message\":\"fixture\"}]}}";
    assert(ts_config_pack_create("unselected-publisher",content,strlen(content),&opts,&bytes,&length)==TS_CONFIG_PACK_OK);
    free(device_key);free(device_cert);device_key=recipient_key;device_cert=recipient_cert;
    ts_config_pack_t *pack=NULL;
    ts_config_pack_result_t result=ts_config_pack_load_mem(bytes,length,&pack);
    if(result==TS_CONFIG_PACK_OK){ts_config_pack_free(pack);fprintf(stderr,"FAIL actual legacy memory loader accepts an unselected publisher\n");return 1;}
    assert(result==TS_CONFIG_PACK_ERR_CERT_CHAIN&&!pack&&!saved_count);
    ts_config_pack_sig_info_t info={.valid=true};
    assert(ts_config_pack_verify_mem(bytes,length,&info)==TS_CONFIG_PACK_ERR_CERT_CHAIN&&!info.valid);
    char temporary[]="/tmp/ts-pack-XXXXXX";assert(mkdtemp(temporary));
    char path[96],plain[96];snprintf(path,sizeof path,"%s/config.tscfg",temporary);snprintf(plain,sizeof plain,"%s/config.json",temporary);
    FILE *file=fopen(path,"wb");assert(file&&fwrite(bytes,1,length,file)==length&&!fclose(file));
    file=fopen(plain,"wb");assert(file&&fputs("{\"fallback\":true}",file)>=0&&!fclose(file));
    assert(ts_config_pack_load(path,&pack)==TS_CONFIG_PACK_ERR_CERT_CHAIN&&!pack);
    assert(ts_config_pack_verify(path,&info)==TS_CONFIG_PACK_ERR_CERT_CHAIN&&!info.valid);
    ts_config_pack_metadata_t metadata={0};assert(ts_config_pack_validate_file(path,&metadata)==TS_CONFIG_PACK_ERR_CERT_CHAIN&&!metadata.sig_info.valid);
    char *selected=NULL;size_t selected_len=0;bool used=false;
    assert(ts_config_pack_load_with_priority(plain,&selected,&selected_len,&used)!=ESP_OK&&!selected&&!selected_len&&!used);
    assert(read_system_file(plain,&selected,&selected_len,&used)==ESP_ERR_NOT_FINISHED&&!selected);
    for(unsigned mode=0;mode<2;mode++){
        cJSON *request=cJSON_CreateObject();cJSON_AddStringToObject(request,mode?"path":"content",mode?path:bytes);ts_api_result_t api={0};
        assert(api_config_pack_verify(request,&api)==ESP_OK&&api.code==TS_API_OK&&cJSON_IsFalse(cJSON_GetObjectItem(api.data,"valid")));ts_api_result_free(&api);
        assert(api_config_pack_import(request,&api)!=ESP_OK&&api.code!=TS_API_OK&&!api.data&&strstr(api.message,"pack_"));ts_api_result_free(&api);cJSON_Delete(request);
    }
    char base[96];snprintf(base,sizeof base,"%s/config",temporary);
    cJSON *legacy=NULL;ts_auto_config_error_t error={0};
    assert(priority_json(base,TS_AUTO_CONFIG_RULE,&legacy,&error)!=ESP_OK&&!legacy&&!strcmp(error.reason,"signer_untrusted"));
    assert(priority_json(base,TS_AUTO_CONFIG_SOURCE,&legacy,&error)!=ESP_OK&&!legacy);
    assert(!file_writes&&!directories&&!saved_count);
    /* Establish fresh trusted admission through the existing Root import, then
     * real boot file reading works with the security clock unavailable. */
    device_key=read_file(dir,"developer.key");device_cert=read_file(dir,"developer.pem");
    char *trusted=NULL;size_t trusted_len=0;
    assert(ts_config_pack_create("boot",content,strlen(content),&opts,&trusted,&trusted_len)==TS_CONFIG_PACK_OK);
    free(device_key);free(device_cert);device_key=recipient_key;device_cert=recipient_cert;
    file=fopen(path,"wb");assert(file&&fwrite(trusted,1,trusted_len,file)==trusted_len&&!fclose(file));
    assert(ts_config_pack_validate_file(path,&metadata)==TS_CONFIG_PACK_OK&&saved_count==1);
    trusted_time=false;used=false;
    assert(read_system_file(plain,&selected,&selected_len,&used)==ESP_OK&&used&&selected_len==strlen(content)&&!strcmp(selected,content));free(selected);
    assert(priority_json(base,TS_AUTO_CONFIG_RULE,&legacy,&error)==ESP_OK&&legacy);
    cJSON *provenance=cJSON_GetObjectItem(cJSON_GetObjectItem(legacy,"rule"),"_package");
    assert(cJSON_IsObject(provenance)&&strlen(cJSON_GetObjectItem(provenance,"package")->valuestring)==64&&cJSON_GetObjectItem(provenance,"policy")->valueint==1);
    cJSON_Delete(legacy);legacy=NULL;
    assert(priority_json(base,TS_AUTO_CONFIG_SOURCE,&legacy,&error)==ESP_OK&&legacy);cJSON_Delete(legacy);
    assert(remove(path)==0&&remove(plain)==0&&rmdir(temporary)==0);
    free(trusted);free(bytes);free(device_key);free(device_cert);free(ca);
    puts("PASS actual file/memory/verify/validate/Root import and boot consumption reject valid-signature unselected publisher; trusted exact admission reloads offline; no plaintext fallback");return 0;
}
