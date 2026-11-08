#define OP_ADAPTER_NO_MAIN
#include "test_operation_adapter.c"
static int scenario;
static terminal_job_t *snapshot_job;
static char terminal_text[64];
static void finish_after_snapshot(void) {
 terminal_output_cb("tail\n",5,snapshot_job);snapshot_job->finished=true;
}
static void capture_terminal(httpd_ws_frame_t *f) {
 cJSON *j=cJSON_ParseWithLength((char*)f->payload,f->len);assert(j);
 cJSON *type=cJSON_GetObjectItem(j,"type"),*data=cJSON_GetObjectItem(j,"data");
 if(cJSON_IsString(type)&&!strcmp(type->valuestring,"output")&&cJSON_IsString(data)){
  assert(strlen(terminal_text)+strlen(data->valuestring)<sizeof(terminal_text));strcat(terminal_text,data->valuestring);
 }cJSON_Delete(j);
}
static void dispatch_terminal(int fd,const char *text){incoming=text;reqs[fd].method=1;current=(void*)3;assert(ws_handler(&reqs[fd])==0);current=(void*)1;incoming=NULL;}
static void during_command(const ts_console_exec_context_t *ctx){
 assert(ctx->remote);
 if(scenario==2){
  snapshot_job=ctx->user_data;ctx->output("first\n",6,ctx->user_data);
  barrier_name="terminal_output_snapshot";barrier_action=finish_after_snapshot;
  terminal_job_maintain(snapshot_job);
  assert(!barrier_action&&s_terminal_output_len==5&&ts_ws_op_is_open(snapshot_job->op));
 }else if(scenario==0){
  assert(!ctx->cancelled(ctx->user_data));ctx->output("ready\n",6,ctx->user_data);
  dispatch_terminal(2,"{\"type\":\"ping\"}");assert(strstr(payloads[2],"pong"));
  dispatch_terminal(1,"{\"type\":\"terminal_input\",\"data\":\"second\"}");assert(strstr(payloads[1],"already running"));
  dispatch_terminal(1,"{\"type\":\"terminal_interrupt\"}");assert(ctx->cancelled(ctx->user_data));
 }else{
  close_peer(1);assert(ctx->cancelled(ctx->user_data));
  reqs[1]=(httpd_req_t){.handle=(void*)11,.fd=1};current=(void*)3;assert(add_client(&reqs[1],WS_CLIENT_TYPE_EVENT)==0);
  assert(start_terminal_session(&reqs[1],NULL)==ESP_FAIL);current=(void*)1;
  ctx->output("must-not-reach-reused-fd",24,ctx->user_data);
 }
}
int main(void){
 cJSON_Hooks h={tracked_malloc,tracked_free};cJSON_InitHooks(&h);
 for(scenario=0;scenario<3;scenario++){
  setup();s_terminal_client_fd=-1;s_output_mutex=xSemaphoreCreateMutex();s_terminal_output_buf=tracked_malloc(TERMINAL_OUTPUT_BUF_SIZE);s_terminal_output_len=0;
  current=(void*)3;assert(start_terminal_session(&reqs[1],NULL)==0);current=(void*)1;
  dispatch_terminal(1,"{\"type\":\"terminal_input\",\"data\":\"forward-fixture\"}");
  assert(exec_task.fn==terminal_job_task && !((terminal_job_t*)exec_task.arg)->finished);console_exec_hook=during_command;if(scenario==2){terminal_text[0]=0;sent_hook=capture_terminal;}
  exec_task.fn(exec_task.arg);console_exec_hook=NULL;drive_all();
  if(scenario==2){assert(!strcmp(terminal_text,"first\ntail\n")&&!ts_ws_op_busy());sent_hook=observe;}
  else if(!scenario)assert(strstr(payloads[1],"done")&&!ts_ws_op_busy());
  else{assert(!strstr(payloads[1],"must-not-reach"));current=(void*)3;assert(start_terminal_session(&reqs[1],NULL)==0);current=(void*)1;}
  tracked_free(s_terminal_output_buf);s_terminal_output_buf=NULL;vSemaphoreDelete(s_output_mutex);s_output_mutex=NULL;finish_ws();
 }
 puts("PASS real terminal worker and completion/output interleaving: request returns before execution; HTTP ping/stop remain available; duplicate command rejected; disconnected/reused fd isolated; new terminal admitted after retirement");
}
