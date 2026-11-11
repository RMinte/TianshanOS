"""Exercise production SCP upload with SSH transport faults and a fresh retry."""
from test_console_lifecycle import body, run_native

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "esp_err.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define LIBSSH2_ERROR_EAGAIN (-37)
#define TS_SSH_STATE_CONNECTED 1
typedef int LIBSSH2_SESSION;
typedef int LIBSSH2_CHANNEL;
typedef void *ts_ssh_session_t;
enum fault { NONE, OPEN_STALL, WRITE_STALL, ACK_STALL, EOF_STALL, WAIT_ERROR,
             ACK_ERROR, SEND_EOF_ERROR, WAIT_EOF_ERROR, CLOSE_ERROR, REMOTE_ERROR };
static enum fault fault;
static unsigned waits, sent, closed;
static bool connected;
static int fixture;

static int ts_ssh_get_state(ts_ssh_session_t s) { return connected; }
static LIBSSH2_SESSION *ts_ssh_get_libssh2_session(ts_ssh_session_t s) { return &fixture; }
static void ts_ssh_operation_begin(ts_ssh_session_t s) { waits=sent=0; }
static bool ts_ssh_operation_expired(ts_ssh_session_t s) { return waits >= 3; }
static int ts_ssh_wait_socket(ts_ssh_session_t s) {
    if (fault==WAIT_ERROR) return -1;
    waits++;
    return ts_ssh_operation_expired(s) ? -1 : 0;
}
static esp_err_t ts_ssh_disconnect(ts_ssh_session_t s) { connected=false; return ESP_OK; }
static LIBSSH2_CHANNEL *libssh2_scp_send(LIBSSH2_SESSION *s,const char *path,int mode,size_t size) {
    assert(!strcmp(path,"archive.tar.gz") && mode==0644 && size==4);
    return fault==OPEN_STALL || fault==WAIT_ERROR ? NULL : &fixture;
}
static int libssh2_session_last_errno(LIBSSH2_SESSION *s) { return LIBSSH2_ERROR_EAGAIN; }
static int libssh2_session_last_error(LIBSSH2_SESSION *s,char **msg,void *unused,int flags) { *msg="fixture"; return 0; }
static ssize_t libssh2_channel_write(LIBSSH2_CHANNEL *c,const char *data,size_t len) {
    if (fault==WRITE_STALL) return LIBSSH2_ERROR_EAGAIN;
    if (sent==4) {
        assert(len==1 && data[0]=='\0');
        sent++;
        return 1;
    }
    assert(len==4-sent && data[0]=="test"[sent]);
    sent++;
    return 1;
}
static ssize_t libssh2_channel_read(LIBSSH2_CHANNEL *c,char *data,size_t len) {
    assert(sent==5 && len==1);
    if (fault==ACK_STALL) return LIBSSH2_ERROR_EAGAIN;
    *data=fault==ACK_ERROR ? 1 : 0;
    return 1;
}
static int libssh2_channel_eof(LIBSSH2_CHANNEL *c) { return 0; }
static int libssh2_channel_send_eof(LIBSSH2_CHANNEL *c) { return fault==SEND_EOF_ERROR ? -1 : 0; }
static int libssh2_channel_wait_eof(LIBSSH2_CHANNEL *c) {
    return fault==WAIT_EOF_ERROR ? -1 : fault==EOF_STALL ? LIBSSH2_ERROR_EAGAIN : 0;
}
static int libssh2_channel_close(LIBSSH2_CHANNEL *c) { return fault==CLOSE_ERROR ? -1 : 0; }
static int libssh2_channel_get_exit_status(LIBSSH2_CHANNEL *c) { return fault==REMOTE_ERROR ? 1 : 0; }
static int libssh2_channel_free(LIBSSH2_CHANNEL *c) { closed++; return 0; }
'''
for name in ('scp_write_buffer', 'ts_scp_send_buffer'):
    code += body('components/ts_security/src/ts_scp.c', name)
code += r'''
static esp_err_t upload(void) {
    return ts_scp_send_buffer(&fixture, (const uint8_t *)"test", 4, "archive.tar.gz", 0);
}

int main(void) {
    for (fault=OPEN_STALL; fault<=REMOTE_ERROR; fault++) {
        connected=true;
        esp_err_t expected=fault<=EOF_STALL ? ESP_ERR_TIMEOUT : ESP_FAIL;
        assert(upload()==expected && !connected);
    }
    fault=NONE; connected=true; closed=0;
    assert(upload()==ESP_OK && connected && sent==5 && closed==1);
    puts("PASS SCP buffer: bounded open/write/EOF, wait/EOF/close/remote errors, clean retry");
}
'''
run_native(code)
