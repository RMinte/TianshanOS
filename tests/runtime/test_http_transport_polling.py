#!/usr/bin/env python3
"""Actual prepared SDK poll functions on socketpairs and actual SDK header parser."""
from pathlib import Path
import importlib.util, os
from test_console_lifecycle import RTOS, IDF, run_native
from host_paths import build_dir

root=Path(__file__).resolve().parents[2]
websocket_source=Path(os.environ.get('TS_WS_COMPONENT_SOURCE',str(root/'managed_components/espressif__esp_websocket_client/esp_websocket_client.c')))
module=importlib.util.spec_from_file_location('transport_preparation',root/'tools/prepare_http_transport_polling.py')
preparation=importlib.util.module_from_spec(module);module.loader.exec_module(preparation)
work=build_dir('http-transport-polling')
original=(IDF/'components/tcp_transport/transport_ssl.c').read_bytes()
metadata=preparation.prepare(IDF,work,websocket_source)
prepared=(work/'transport_ssl.c').read_text()
preparation.prepare(IDF,work,websocket_source)
assert (work/'transport_ssl.c').read_text()==prepared and (IDF/'components/tcp_transport/transport_ssl.c').read_bytes()==original
functions=[]
for name in ('base_poll_read','base_poll_write'):
    start=prepared.index('static int '+name+'(')
    end=prepared.index('\n}\n',start)+3
    functions.append(prepared[start:end])
code=RTOS+r'''
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#define ESP_LOGV(...) ((void)0)
#define ESP_STATIC_ANALYZER_CHECK(condition,result) do {if(condition)return(result);} while(0)
typedef struct {int sockfd;void *tls;} transport_esp_tls_t;
typedef transport_esp_tls_t *esp_transport_handle_t;
static transport_esp_tls_t *ssl_get_context_data(esp_transport_handle_t transport){return transport;}
static int esp_tls_get_bytes_avail(void *tls){(void)tls;return 0;}
static void esp_transport_capture_errno(esp_transport_handle_t transport,int error){(void)transport;(void)error;}
static struct timeval *esp_transport_utils_ms_to_timeval(int ms,struct timeval *value){if(ms<0)return NULL;value->tv_sec=ms/1000;value->tv_usec=(ms%1000)*1000;return value;}
'''
code+='\n'.join(functions)+'\n#define HTTP_MAX_HEADER_SIZE 16384\n#include "'+str(IDF/'components/http_parser/http_parser.c')+'"\n'
client_prepared=(work/'esp_http_client.c').read_text()
start=client_prepared.index('static int esp_http_client_get_data(')
end=client_prepared.index('\n}\n',start)+3
code+=r'''
#define HTTP_STATE_RES_ON_DATA_START 4
#define HTTP_METHOD_HEAD 2
#define ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN -3
typedef struct {char *data;int len;} esp_http_buffer_t;
typedef struct {esp_http_buffer_t *buffer;int64_t data_process,content_length;} fixture_response_t;
typedef struct fixture_client {int state,timeout_ms;bool is_async,is_chunk_complete;struct {int method;} connection_info;fixture_response_t *response;http_parser *parser;http_parser_settings *parser_settings;void *transport;} fixture_client_t;
typedef fixture_client_t *esp_http_client_handle_t;
static int transport_read_result;
static int esp_transport_read(void *transport,char *data,int length,int timeout){(void)transport;(void)data;(void)length;(void)timeout;return transport_read_result;}
'''
# Actual prepared SDK get_data uses its configured receive buffer size.
code=code.replace('int state,timeout_ms;bool','int state,timeout_ms,buffer_size_rx;bool')
code+=client_prepared[start:end]
code+=r'''
static unsigned checks;static size_t body_bytes;
static void check(bool value,const char *message){checks++;if(!value){fprintf(stderr,"FAIL %s\n",message);abort();}}
static int body(http_parser *parser,const char *bytes,size_t length){(void)parser;(void)bytes;body_bytes+=length;return 0;}
static void header_size(size_t length,bool valid){
 char *message=malloc(length+128);assert(message);size_t prefix=(size_t)sprintf(message,"HTTP/1.1 200 OK\r\nX-Fixture: ");
 memset(message+prefix,'x',length);const char *end="\r\nContent-Length: 0\r\n\r\n";memcpy(message+prefix+length,end,strlen(end));size_t total=prefix+length+strlen(end);
 http_parser parser;http_parser_init(&parser,HTTP_RESPONSE);http_parser_settings settings={0};size_t parsed=http_parser_execute(&parser,&settings,message,total);
 check(valid?(parsed==total&&HTTP_PARSER_ERRNO(&parser)==HPE_OK):HTTP_PARSER_ERRNO(&parser)==HPE_HEADER_OVERFLOW,"actual SDK parser enforces selected full-header limit without reimplementing parsing");free(message);
}
static int eof_complete(http_parser *parser){((esp_http_client_handle_t)parser->data)->is_chunk_complete=true;return 0;}
static void eof_case(const char *message,int read_result,bool complete){
 http_parser parser;http_parser_init(&parser,HTTP_RESPONSE);http_parser_settings settings={.on_message_complete=eof_complete};
 char bytes[64];esp_http_buffer_t buffer={.data=bytes};fixture_response_t response={.buffer=&buffer};
 fixture_client_t client={.state=HTTP_STATE_RES_ON_DATA_START,.is_async=true,.response=&response,.parser=&parser,.parser_settings=&settings};
 parser.data=&client;assert(http_parser_execute(&parser,&settings,message,strlen(message))==strlen(message));
 transport_read_result=read_result;errno=EAGAIN;int result=esp_http_client_get_data((esp_http_client_handle_t)&client);
 check(complete?(result==0&&client.is_chunk_complete&&HTTP_PARSER_ERRNO(&parser)==HPE_OK&&errno==0):!client.is_chunk_complete,"confirmed FIN completes only legal EOF framing; short CL/chunks, resets and retry-without-data never become complete");
}
int main(void){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));transport_esp_tls_t transport={.sockfd=pair[0]};
 errno=EIO;check(base_poll_read(&transport,0)==0&&errno==EAGAIN,"zero-timeout no-data poll is retryable nonblocking I/O");
 errno=EIO;check(base_poll_read(&transport,1)==0&&errno==EIO,"positive blocking-timeout errno behavior is unchanged");
 assert(write(pair[1],"a",1)==1);errno=EAGAIN;
 check(base_poll_read(&transport,0)>0&&errno==0,"read readiness cannot inherit stale EAGAIN");char byte;assert(read(pair[0],&byte,1)==1);
 errno=EAGAIN;check(base_poll_write(&transport,0)>0&&errno==0,"write readiness cannot hide a later real write failure behind stale EAGAIN");
 int flags=fcntl(pair[0],F_GETFL,0);assert(flags>=0&&!fcntl(pair[0],F_SETFL,flags|O_NONBLOCK));
 char block[4096];memset(block,'x',sizeof(block));while(write(pair[0],block,sizeof(block))>0){}assert(errno==EAGAIN||errno==EWOULDBLOCK);
 errno=EIO;check(base_poll_write(&transport,0)==0&&errno==EAGAIN,"full send buffer yields EAGAIN without blocking");
 close(pair[0]);errno=0;check(base_poll_read(&transport,0)<0&&errno==EBADF,"actual select error retains its error instead of becoming retryable");close(pair[1]);
 header_size(8192,true);header_size(16300,true);header_size(16400,false);
 http_parser parser;http_parser_init(&parser,HTTP_RESPONSE);http_parser_settings settings={.on_body=body};
 const char *header="HTTP/1.1 200 OK\r\nContent-Length: 32768\r\n\r\n";assert(http_parser_execute(&parser,&settings,header,strlen(header))==strlen(header));
 char bytes[32768];memset(bytes,0x5a,sizeof(bytes));check(http_parser_execute(&parser,&settings,bytes,sizeof(bytes))==sizeof(bytes)&&body_bytes==sizeof(bytes)&&HTTP_PARSER_ERRNO(&parser)==HPE_OK,"header budget does not truncate legitimate binary bodies larger than 16KiB");
 eof_case("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody",ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN,true);
 eof_case("HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nbody",ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN,false);
 eof_case("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nbody\r\n",ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN,false);
 eof_case("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody",-7,false);
 eof_case("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbody",0,false);
 printf("%u checks: actual project-prepared SDK poll functions on local socketpairs, actual SDK HTTP parser, idempotent generation and unchanged SDK checkout; no remote service\n",checks);
}
'''
run_native(code)
