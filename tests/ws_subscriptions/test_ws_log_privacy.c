/* Execute actual start/worker/matcher/variable path; only remote I/O is synthetic. */
#define OP_ADAPTER_NO_MAIN
#define TEST_CAPTURE_DIAGNOSTICS
#include "test_operation_adapter.c"
int main(void){
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);setup();sent_hook=observe;
 diagnostic_used=0;diagnostic_trace[0]=0;
 const char *command="echo Synthetic-Command-Credential";
 const char *parts[]={"Synthetic-Extract-Pattern=Synthetic-Private-Result\n",NULL};
 matching_parts=parts;expected_command=command;
 ts_webui_ssh_options_t options={.collect_output=true,.expect_pattern="Synthetic-Expect-Pattern",.fail_pattern="Synthetic-Fail-Pattern",.extract_pattern="Synthetic-Extract-Pattern=(.*)",.var_name="private_fixture"};
 unsigned writes=variable_writes,submitted=commands_submitted,old_aborts=aborts;uint32_t id;
 assert(ts_webui_ssh_exec_start_ex("fixture.invalid",22,"fixture",NULL,"Synthetic-Authentication-Password",command,&options,&id)==ESP_OK);
 exec_task.fn(exec_task.arg);finish_all();
 assert(commands_submitted==submitted+1&&variable_writes>writes&&aborts==old_aborts);
 assert(!strcmp(stored_extract,"Synthetic-Private-Result"));
 const char *private_values[]={"Synthetic-Command-Credential","Synthetic-Authentication-Password","Synthetic-Expect-Pattern","Synthetic-Fail-Pattern","Synthetic-Extract-Pattern","Synthetic-Private-Result"};
 for(unsigned i=0;i<sizeof(private_values)/sizeof(*private_values);i++)assert(!strstr(diagnostic_trace,private_values[i]));
 assert(strstr(diagnostic_trace,"command_bytes=")&&strstr(diagnostic_trace,"has_expect=1")&&strstr(diagnostic_trace,"Realtime extraction complete")&&strstr(diagnostic_trace,"private_fixture"));
 puts("PASS real SSH start/worker/matcher: command/pattern/password/extracted payloads absent from global diagnostics; original command executed once, exact value stored, result/cleanup retained");
}
