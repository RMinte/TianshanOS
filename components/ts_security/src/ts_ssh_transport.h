#pragma once

#include "ts_ssh_client.h"

void ts_ssh_operation_begin(ts_ssh_session_t session);
bool ts_ssh_operation_expired(ts_ssh_session_t session);
int ts_ssh_wait_socket(ts_ssh_session_t session);
