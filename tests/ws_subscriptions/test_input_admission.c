/* Entire production WS unit/controller/transport, synthetic credentials and socket I/O. */
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#define OP_ADAPTER_NO_MAIN
#include "test_operation_adapter.c"
#ifndef TS_WS_INPUT_MAX_BYTES
#define TS_WS_INPUT_MAX_BYTES (64 * 1024)
#endif
esp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *out){if(sid!=7)return ESP_ERR_NOT_FOUND;if(out)*out=(ts_session_t){.session_id=7,.level=TS_PERM_ROOT};return ESP_OK;}
esp_err_t ts_auth_validate_request(const char *header,uint32_t *sid,ts_perm_level_t *level){if(strcmp(header,"Bearer root-token"))return ESP_ERR_NOT_FOUND;*sid=7;*level=TS_PERM_ROOT;return ESP_OK;}
static unsigned checks,failures;
static void *json_oom(size_t size){(void)size;return NULL;}
static void expect(bool ok,const char *label){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static esp_err_t input(const char *body,size_t length,int type,bool final,bool override){
 incoming=body;incoming_length=length;incoming_type=type;incoming_final=final;incoming_override=override;
 reqs[1].method=1;current=(void*)3;esp_err_t ret=ws_handler(&reqs[1]);current=(void*)1;
 incoming=NULL;incoming_override=false;incoming_final=true;incoming_type=1;return ret;
}
static void shell_start(void){setup();const char *j="{\"type\":\"ssh_connect\",\"token\":\"root-token\",\"host\":\"fake\",\"user\":\"u\"}";assert(input(j,strlen(j),1,true,false)==ESP_OK&&ts_ws_op_busy());}
static void shell_finish(void){sent_hook=NULL;close_peer(1);shell_task.fn(shell_task.arg);finish_all();}
static int observed_type;static size_t observed_len;static uint8_t observed_control[125];
static void observe_control(httpd_ws_frame_t *frame){observed_type=frame->type;observed_len=frame->len;if(frame->len<=sizeof(observed_control))memcpy(observed_control,frame->payload,frame->len);}
int main(void){
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);
 const char *bad[]={"{","[]","null","{\"type\":\"ssh_input\",\"data\":\"probe\"}tail","{\"type\":\"ssh_input\",\"TYPE\":\"ping\",\"data\":\"probe\"}","{\"type\":\"ssh_input\",\"data\":\"probe\",\"data\":\"other\"}","{\"type\":\"ssh_input\",\"data\":\"probe\\u0000tail\"}"};
 shell_start();
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
  unsigned writes=control_writes,before=frames[1];expect(input(bad[i],strlen(bad[i]),1,true,false)==ESP_OK,"invalid JSON sends an error without corrupting the connection");
  expect(control_writes==writes&&frames[1]==before+1&&strstr(payloads[1],"Invalid JSON message"),"malformed or ambiguous JSON never writes SSH input");
 }
 char nul[]="{\"type\":\"ssh_input\",\"data\":\"probe\"}\0tail";unsigned before=control_writes;
 expect(input(nul,sizeof(nul)-1,1,true,true)==ESP_OK&&control_writes==before,"embedded NUL cannot execute a valid prefix");
 cJSON_Hooks oom={json_oom,tracked_free};cJSON_InitHooks(&oom);unsigned writes=control_writes,frames_before=frames[1];
 const char *valid="{\"type\":\"ssh_input\",\"data\":\"probe\"}";
 expect(input(valid,strlen(valid),1,true,false)==ESP_OK&&control_writes==writes&&frames[1]==frames_before+1,"JSON allocation failure never writes SSH input and reports an error");cJSON_InitHooks(&hooks);
 before=control_writes;const char *good="{\"type\":\"ssh_input\",\"data\":\"中文 + \\\"quoted\\\"\"}";
 expect(input(good,strlen(good),1,true,false)==ESP_OK&&control_writes==before+1,"normal Unicode SSH stream input preserved");shell_finish();
 const int types[]={1,2,0};
 for(unsigned i=0;i<3;i++){
  shell_start();const char *j="{\"type\":\"ssh_input\",\"data\":\"probe\"}";unsigned writes=control_writes,reads=input_payloads;
  expect(input(j,strlen(j),types[i],i!=0,false)!=ESP_OK,"fragment, binary and continuation frames rejected");
  expect(control_writes==writes&&input_payloads==reads,"unsupported framing rejected before reading or executing payload");shell_finish();
 }
 const size_t lengths[]={TS_WS_INPUT_MAX_BYTES+1,SIZE_MAX};
 for(unsigned i=0;i<2;i++){
  setup();unsigned reads=input_payloads,allocated=allocations;incoming_read_fail=true;
  expect(input("{}",lengths[i],1,true,true)!=ESP_OK,"oversize frame rejected");incoming_read_fail=false;
  expect(input_payloads==reads&&allocations==allocated,"declared oversize and addition overflow allocate/read nothing");finish_all();
 }
 setup();size_t size=TS_WS_INPUT_MAX_BYTES;char *large=malloc(size+1);memset(large,'x',size);const char *prefix="{\"type\":\"ping\",\"padding\":\"";memcpy(large,prefix,strlen(prefix));large[size-2]='"';large[size-1]='}';large[size]=0;
 expect(input(large,size,1,true,false)==ESP_OK&&strstr(payloads[1],"pong"),"maximum legal complete text frame remains usable");free(large);finish_all();
 setup();const char *j="{\"type\":\"ping\"}";unsigned reads=input_payloads;rejected_allocation_size=strlen(j)+1;
 expect(input(j,strlen(j),1,true,false)==ESP_ERR_NO_MEM&&input_payloads==reads,"PSRAM and fallback buffer allocation failure performs no payload read");rejected_allocation_size=0;finish_all();
 setup();incoming_read_fail=true;reads=input_payloads;expect(input(j,strlen(j),1,true,false)!=ESP_OK&&input_payloads==reads+1,"short/error receive never parses a message");incoming_read_fail=false;finish_all();
 setup();sent_hook=observe_control;observed_type=0;const char ping[]={1,2,3,4};unsigned allocated=allocations;
 expect(input(ping,sizeof(ping),HTTPD_WS_TYPE_PING,true,true)==ESP_OK&&observed_type==HTTPD_WS_TYPE_PONG&&observed_len==sizeof(ping)&&!memcmp(observed_control,ping,sizeof(ping)),"standard Ping receives identical Pong payload");
 expect(allocations==allocated,"control frames use bounded stack storage");observed_type=0;
 expect(input(ping,sizeof(ping),HTTPD_WS_TYPE_PONG,true,true)==ESP_OK&&observed_type==0,"standard Pong does not produce a reply loop");sent_hook=NULL;finish_all();
 char oversized_control[126];memset(oversized_control,'x',sizeof(oversized_control));
 setup();reads=input_payloads;expect(input(oversized_control,sizeof(oversized_control),HTTPD_WS_TYPE_PING,true,true)!=ESP_OK&&input_payloads==reads,"oversize control frame rejected before payload read");finish_all();
 setup();reads=input_payloads;expect(input("x",1,HTTPD_WS_TYPE_CLOSE,true,true)!=ESP_OK&&input_payloads==reads,"invalid one-byte Close rejected");finish_all();
 setup();sent_hook=observe_control;observed_type=0;expect(input("",0,HTTPD_WS_TYPE_CLOSE,true,true)==ESP_OK&&observed_type==HTTPD_WS_TYPE_CLOSE&&observed_len==0,"normal Close acknowledgement preserved");sent_hook=NULL;finish_all();
 printf("%u checks, %u failures: complete production WS admission, real parser, synthetic SDK frames; no device\n",checks,failures);return failures?1:0;
}
