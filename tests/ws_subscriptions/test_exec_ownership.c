/* Whole production WS/controller/transport, authenticated synthetic I/O. */
#define OP_ADAPTER_NO_MAIN
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#include "test_operation_adapter.c"
static bool owner_live=true;
static ts_perm_level_t owner_level=TS_PERM_ROOT;
esp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *out){
 if(sid<7||sid>11||(sid==7&&!owner_live))return ESP_ERR_NOT_FOUND;
 ts_perm_level_t levels[]={TS_PERM_ROOT,TS_PERM_ROOT,TS_PERM_ADMIN,TS_PERM_READ,TS_PERM_WRITE};
 if(out)*out=(ts_session_t){.session_id=sid,.level=sid==7?owner_level:levels[sid-7]};return ESP_OK;
}
esp_err_t ts_auth_validate_request(const char *header,uint32_t *sid,ts_perm_level_t *level){
 const char *tokens[]={"Bearer owner","Bearer other","Bearer admin","Bearer read","Bearer write"};
 for(unsigned i=0;i<5;i++)if(!strcmp(header,tokens[i])){ts_session_t s;if(ts_security_validate_session(i+7,&s)!=ESP_OK)return ESP_ERR_NOT_FOUND;*sid=i+7;*level=s.level;return ESP_OK;}return ESP_ERR_NOT_FOUND;
}
static ts_ws_peer_t bound_peer(int fd){return *(ts_ws_peer_t*)reqs[fd].sess_ctx;}
static void authenticate(int fd,const char *token){
 char text[128];snprintf(text,sizeof(text),"{\"type\":\"auth\",\"token\":\"%s\",\"owner_session_id\":7,\"level\":4}",token);
 reqs[fd].method=1;incoming=text;current=(void*)3;assert(ws_handler(&reqs[fd])==ESP_OK);incoming=NULL;current=(void*)1;assert(strstr(payloads[fd],"auth_ok"));
}
static unsigned raw[32],matches[32],done[32];
static void observe_private(httpd_ws_frame_t *f){
 cJSON *m=cJSON_ParseWithLength((char*)f->payload,f->len);assert(m);const cJSON *t=cJSON_GetObjectItem(m,"type");
 if(cJSON_IsString(t)&&!strncmp(t->valuestring,"ssh_exec_",9)){raw[sent_fd]++;if(!strcmp(t->valuestring,"ssh_exec_match"))matches[sent_fd]++;if(!strcmp(t->valuestring,"ssh_exec_done"))done[sent_fd]++;assert(!cJSON_HasObjectItem(m,"boot_id")&&!cJSON_HasObjectItem(m,"result_key"));}cJSON_Delete(m);
}
static void private_setup(void){
 setup();owner_live=true;owner_level=TS_PERM_ROOT;memset(raw,0,sizeof(raw));memset(matches,0,sizeof(matches));memset(done,0,sizeof(done));sent_hook=observe_private;
 for(int fd=3;fd<=8;fd++)open_peer(fd);
 authenticate(1,"owner");authenticate(2,"owner");authenticate(3,"other");authenticate(4,"admin");authenticate(5,"read");authenticate(6,"write");authenticate(8,"read");
 assert(ts_ws_peer_authorized(bound_peer(3),TS_PERM_ROOT));assert(!ts_ws_peer_authorized_for_session(bound_peer(3),TS_PERM_ROOT,7));
 ts_ws_peer_t peers[TS_WS_CONNECTIONS];assert(ts_ws_peer_snapshot_for_session(peers,TS_WS_CONNECTIONS,TS_PERM_ROOT,7)==2);assert(!ts_ws_peer_snapshot_for_session(peers,TS_WS_CONNECTIONS,TS_PERM_ROOT,0));
}
static void no_other_receivers(void){for(unsigned fd=3;fd<=8;fd++)assert(raw[fd]==0);}
static void matching_output(void){ssh_exec_task_params_t *p=exec_task.arg;ssh_exec_output_callback("REA",3,false,p);drive_all();ssh_exec_output_callback("DY value=",9,false,p);ssh_exec_output_callback("42;",3,false,p);assert(p->output_buffer&&strstr(p->output_buffer,"READY value=42;"));drive_all();}
static void run_business(unsigned owner, bool zero_observers){
 ts_webui_ssh_receipt_t receipt={0};ts_webui_ssh_options_t options={.owner_session_id=owner,.receipt=&receipt,.collect_output=true,.expect_pattern="READY",.extract_pattern="value=(.*);",.var_name="fake"};
 unsigned submitted=commands_submitted,writes=variable_writes;uint32_t id;
 remote_hook=matching_output;assert(ts_webui_ssh_exec_start_ex("fake",22,"u",NULL,"p","cmd",&options,&id)==ESP_OK);
 exec_task.fn(exec_task.arg);remote_hook=NULL;drive_all();assert(commands_submitted==submitted+1&&variable_writes>writes+7);
 char *text=ts_webui_ssh_exec_status_for_session(id,receipt.boot_id,receipt.result_key,owner,NULL);assert(text);
 cJSON *result=cJSON_Parse(text);tracked_free(text);assert(result&&cJSON_GetObjectItem(result,"output_bytes")->valuedouble>=15);
 assert(!strcmp(cJSON_GetObjectItem(result,"execution_status")->valuestring,"success")&&cJSON_IsTrue(cJSON_GetObjectItem(result,"expect_matched"))&&!strcmp(cJSON_GetObjectItem(result,"extracted")->valuestring,"42"));cJSON_Delete(result);assert(!strcmp(stored_extract,"42"));
 no_other_receivers();if(zero_observers)assert(!raw[1]&&!raw[2]);else{assert(raw[1]&&raw[2]&&matches[1]&&matches[2]&&done[1]==1&&done[2]==1);}
 finish_all();
}
static void queued_checks(unsigned mode){
 private_setup();ts_webui_ssh_receipt_t receipt;ts_webui_ssh_options_t options={.owner_session_id=7,.receipt=&receipt};uint32_t id;
 assert(ts_webui_ssh_exec_start_ex("fake",22,"u",NULL,"p","cmd",&options,&id)==ESP_OK);ssh_exec_task_params_t *p=exec_task.arg;
 ts_ws_output_ticket_t ticket;assert(ts_ws_op_output_begin(p->op,&ticket)&&ticket.count==2);
 ts_ws_op_output_finish(&ticket,"{\"type\":\"ssh_exec_output\",\"data\":\"queued private\"}");
 if(mode==0)owner_live=false;
 if(mode==1)owner_level=TS_PERM_ADMIN;
 if(mode==2){close_peer(1);open_peer(1);authenticate(1,"other");}
 pump();no_other_receivers();assert(!raw[1]);if(mode==2)assert(raw[2]==1);else assert(!raw[2]);
 ts_ws_op_stats_t stats;ts_ws_op_stats(TS_WS_OP_EXEC,&stats);assert(!stats.unsettled&&stats.failed==(mode==2)&&ts_ws_op_business_running(p->op));
 if(mode==2){close_peer(1);open_peer(1);authenticate(1,"owner");}
 owner_live=true;owner_level=TS_PERM_ROOT;unsigned before=commands_submitted;
 exec_task.fn(exec_task.arg);drive_all();assert(commands_submitted==before+1);no_other_receivers();if(mode==2)assert(raw[1]&&done[1]==1);
 finish_all();
}
static void expire_before_final(void){owner_live=false;}
static void final_delivery_check(void){
 private_setup();ts_webui_ssh_receipt_t receipt;ts_webui_ssh_options_t options={.owner_session_id=7,.receipt=&receipt};uint32_t id;unsigned submitted=commands_submitted;
 assert(ts_webui_ssh_exec_start_ex("fake",22,"u",NULL,"p","cmd",&options,&id)==ESP_OK);
 barrier_name="B08";barrier_action=expire_before_final;exec_task.fn(exec_task.arg);drive_all();assert(!barrier_action&&commands_submitted==submitted+1&&!done[1]&&!done[2]);no_other_receivers();
 char *result=ts_webui_ssh_exec_status(id,receipt.boot_id,receipt.result_key,NULL);assert(result&&strstr(result,"ssh_exec_done"));tracked_free(result);owner_live=true;finish_all();
}
int main(void){
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);
 private_setup();run_business(7,false);
 private_setup();close_peer(1);close_peer(2);run_business(7,true);
 private_setup();run_business(0,true);
 for(unsigned i=0;i<3;i++)queued_checks(i);
 final_delivery_check();
 private_setup();ts_ws_message_t *m=ts_ws_message_text("{\"type\":\"ssh_exec_output\"}",strlen("{\"type\":\"ssh_exec_output\"}"));assert(m);ts_ws_message_require(m,TS_PERM_ROOT);ts_ws_message_require_session(m,7);
 assert(ts_ws_transport_submit(bound_peer(3),m,0,0,NULL,NULL)==ESP_OK);ts_ws_message_release(m);pump();assert(!raw[3]);
 /* A zero-owner restriction remains deny-all even if submitted directly. */
 m=ts_ws_message_text("{\"type\":\"ssh_exec_output\"}",strlen("{\"type\":\"ssh_exec_output\"}"));assert(m);ts_ws_message_require_session(m,0);assert(ts_ws_transport_submit(bound_peer(1),m,0,0,NULL,NULL)==ESP_OK);ts_ws_message_release(m);pump();assert(!raw[1]);
 finish_all();
 puts("PASS private EXEC: only same-session Root receives start/bytes/match/final; other Root/Admin/READ/WRITE/unbound/forged peers see zero; queued revoke/downgrade/fd reuse rechecked; reconnect works; zero observers/internal owner=0 preserve actual command/result/extraction/variable writes");
}
