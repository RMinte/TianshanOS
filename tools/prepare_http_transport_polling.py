#!/usr/bin/env python3
"""Prepare build-local SDK HTTP I/O and private diagnostics; never edit ESP-IDF."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def prepare(idf, output, websocket_source):
    source_path = Path(idf) / 'components/tcp_transport/transport_ssl.c'
    source = source_path.read_text()
    if 'esp_transport_set_async_connect_func(tcp_transport, tcp_connect_async);' not in source:
        raise ValueError('SDK TCP asynchronous connection support changed; review required')
    result = source
    for name in ('base_poll_read', 'base_poll_write'):
        start = result.index('static int ' + name + '(')
        end = result.index('\n}\n', start) + 3
        function = result[start:end]
        if function.count('    return ret;') != 1 or 'ret = select(' not in function or re.search(r'^\s*errno\s*=', function, re.M):
            raise ValueError('SDK ' + name + ' polling contract changed; review required')
        patched = function.replace('    return ret;', '''    /* Zero-timeout polling is nonblocking I/O: no readiness means EAGAIN,
     * not a completed/truncated HTTP response. Positive readiness must not
     * inherit a prior EAGAIN and hide a subsequent real EOF/write failure. */
    if (timeout_ms == 0 && ret >= 0) errno = ret == 0 ? EAGAIN : 0;
    return ret;''')
        result = result[:start] + patched + result[end:]
    client_source_path = Path(idf) / 'components/esp_http_client/esp_http_client.c'
    client_source = client_source_path.read_text()
    start = client_source.index('static int esp_http_client_get_data(')
    end = client_source.index('\n}\n', start) + 3
    function = client_source[start:end]
    if function.count('    return rlen;') != 1 or 'http_parser_execute(client->parser' not in function:
        raise ValueError('SDK HTTP EOF parsing contract changed; review required')
    patched = function.replace('    return rlen;', '''    /* FIN is a real EOF, unlike a zero-timeout retry. Let the SDK parser
     * complete legal close-delimited bodies; incomplete CL/chunks still fail. */
    if (rlen == ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN) {
        http_parser_execute(client->parser, client->parser_settings, res_buffer->data, 0);
        if (HTTP_PARSER_ERRNO(client->parser) == HPE_OK && client->is_chunk_complete) {
            errno = 0;
            return 0;
        }
    }
    return rlen;''')
    client_result = client_source[:start] + patched + client_source[end:]
    # Protocol contents must reach their actual caller, never the global log.
    # Exact SDK anchors are reviewed with the I/O adaptations; a changed SDK
    # cannot silently restore Authorization/cookie/query logging.
    private_diagnostics = [
        ('ESP_LOGD(TAG, "HEADER=%s:%s", client->current_header_key, client->current_header_value);', 'ESP_LOGD(TAG, "HTTP response header received");', 1),
        ('ESP_LOGD(TAG, "auth_response=%s", auth_response);', 'ESP_LOGD(TAG, "HTTP authorization prepared");', 2),
        ('ESP_LOGD(TAG, "Redirect to %s", client->location);', 'ESP_LOGD(TAG, "HTTP redirect received");', 1),
        ('ESP_LOGE(TAG, "Error parse url %s", url);', 'ESP_LOGE(TAG, "Invalid HTTP URL");', 1),
        ('ESP_LOGD(TAG, "New host assign = %s", client->connection_info.host);', 'ESP_LOGD(TAG, "HTTP host updated");', 1),
        ('ESP_LOGD(TAG, "Begin connect to: %s://%s:%d", client->connection_info.scheme, client->connection_info.host, client->connection_info.port);', 'ESP_LOGD(TAG, "Opening HTTP connection");', 1),
        ('ESP_LOGD(TAG, "Write header[%d]: %s", client->header_index, client->request->buffer->data);', 'ESP_LOGD(TAG, "Writing HTTP headers (part=%d)", client->header_index);', 1),
        ('ESP_LOGD(TAG, "UNAUTHORIZED: %s", auth_header);', 'ESP_LOGD(TAG, "HTTP authentication challenge received");', 1),
        ('ESP_LOGE(TAG, "This authentication method is not supported: %s", auth_header);', 'ESP_LOGE(TAG, "Unsupported HTTP authentication method");', 1),
    ]
    for original, safe, expected in private_diagnostics:
        if client_result.count(original) != expected:
            raise ValueError('SDK HTTP private diagnostic contract changed; review required')
        client_result = client_result.replace(original, safe)
    ws_source_path = Path(idf) / 'components/tcp_transport/transport_ws.c'
    ws_source = ws_source_path.read_text()
    ws_result = ws_source
    ws_diagnostics = [
        ('ESP_LOGE(TAG, "Error connecting to host %s:%d", host, port);', 'ESP_LOGE(TAG, "WebSocket transport connection failed");'),
        ('ESP_LOGD(TAG, "sub_protocol: %s", ws->sub_protocol);', 'ESP_LOGD(TAG, "WebSocket subprotocol configured");'),
        ('ESP_LOGD(TAG, "Authorization: %s", ws->auth);', 'ESP_LOGD(TAG, "WebSocket authorization configured");'),
        ('ESP_LOGD(TAG, "headers: %s", ws->headers);', 'ESP_LOGD(TAG, "WebSocket headers configured");'),
        ('ESP_LOGD(TAG, "Write upgrade request\\r\\n%s", ws->buffer);', 'ESP_LOGD(TAG, "Writing WebSocket upgrade request");'),
        ('ESP_LOGE(TAG, "Error write Upgrade header %s", ws->buffer);', 'ESP_LOGE(TAG, "Writing WebSocket upgrade request failed");'),
        ('ESP_LOGD(TAG, "server key=%s, send_key=%s, expected_server_key=%s", (char *)server_key, (char *)client_key, expected_server_key);', 'ESP_LOGD(TAG, "WebSocket handshake response received");'),
    ]
    websocket_source = Path(websocket_source)
    websocket_original = websocket_source.read_text()
    websocket_result = websocket_original
    websocket_diagnostics = [
        ('ESP_LOGE(TAG, "%s", client->errormsg_buffer);', 'ESP_LOGE(TAG, "WebSocket client error (%d bytes)", needed_size);'),
        ('ESP_LOGE(TAG, "Not support this websocket scheme %s, only support %s and %s", client->config->scheme, WS_OVER_TCP_SCHEME, WS_OVER_TLS_SCHEME);', 'ESP_LOGE(TAG, "Unsupported WebSocket scheme; use ws or wss");'),
        ('ESP_LOGE(TAG, "Error parse uri = %s", uri);', 'ESP_LOGE(TAG, "Invalid WebSocket URI");'),
        ('ESP_LOGD(TAG, "Transport connected to %s://%s:%d", client->config->scheme, client->config->host, client->config->port);', 'ESP_LOGD(TAG, "WebSocket transport connected");'),
    ]
    for original, safe in ws_diagnostics:
        if ws_result.count(original) != 1:
            raise ValueError('SDK WebSocket private diagnostic contract changed; review required')
        ws_result = ws_result.replace(original, safe)
    for original, safe in websocket_diagnostics:
        if websocket_result.count(original) != 1:
            raise ValueError('Managed WebSocket private diagnostic contract changed; review required')
        websocket_result = websocket_result.replace(original, safe)
    # The SDK consumes the argument list during sizing. Copy it so private
    # application error events retain their exact arguments on every ABI.
    original = """    size_t needed_size = vsnprintf(NULL, 0, format, myargs);
    needed_size++; // null terminator"""
    replacement = """    va_list sizing_args;
    va_copy(sizing_args, myargs);
    int needed_size = vsnprintf(NULL, 0, format, sizing_args);
    va_end(sizing_args);
    if (needed_size < 0) {
        va_end(myargs);
        return ESP_FAIL;
    }
    size_t allocation_size = (size_t)needed_size + 1;"""
    replacements = [
        (original, replacement),
        ('if (needed_size > client->errormsg_size)', 'if (allocation_size > client->errormsg_size)'),
        ('client->errormsg_buffer = malloc(needed_size);', 'client->errormsg_buffer = malloc(allocation_size);'),
        ('client->errormsg_size = needed_size;', 'client->errormsg_size = allocation_size;'),
        ('            client->errormsg_size = 0;\n            ESP_LOGE(TAG, "Failed to allocate...");', '            client->errormsg_size = 0;\n            va_end(myargs);\n            ESP_LOGE(TAG, "Failed to allocate...");'),
    ]
    for original, replacement in replacements:
        if websocket_result.count(original) != 1:
            raise ValueError('Managed WebSocket error formatting contract changed; review required')
        websocket_result = websocket_result.replace(original, replacement)
    # Publish the running lifetime before the worker can finish. A worker may
    # set STOPPED before xTaskCreate returns; clearing it afterwards loses that
    # completion and makes stop/destroy wait forever. Failed creation is stopped.
    start = websocket_result.index('esp_err_t esp_websocket_client_start(')
    end = websocket_result.index('\n}', start) + 2
    body = websocket_result[start:end]
    clear = '    xEventGroupClearBits(client->status_bits, STOPPED_BIT | CLOSE_FRAME_SENT_BIT);\n'
    create = '    if (xTaskCreate(esp_websocket_client_task,'
    failure = '        ESP_LOGE(TAG, "Error create websocket task");\n        return ESP_FAIL;'
    if body.count(clear) != 1 or body.count(create) != 1 or body.count(failure) != 1:
        raise ValueError('Managed WebSocket task publication contract changed; review required')
    body = body.replace(clear, '').replace(create, clear + '\n' + create)
    body = body.replace(failure, '        xEventGroupSetBits(client->status_bits, STOPPED_BIT);\n' + failure)
    websocket_result = websocket_result[:start] + body + websocket_result[end:]
    # STOPPED is a publication boundary. The SDK previously touched client
    # after setting it, while destroy could already free client/event storage.
    # Keep the existing client mutex until SetBits itself has returned; a
    # waiting destructor crosses that mutex before freeing any SDK resources.
    old_finish = '    xEventGroupSetBits(client->status_bits, STOPPED_BIT);\n    client->state = WEBSOCKET_STATE_UNKNOW;\n    if (client->selected_for_destroying == true) {\n        destroy_and_free_resources(client);\n    }'
    new_finish = '    xSemaphoreTakeRecursive(client->lock, portMAX_DELAY);\n    client->state = WEBSOCKET_STATE_UNKNOW;\n    if (client->selected_for_destroying == true) {\n        xSemaphoreGiveRecursive(client->lock);\n        destroy_and_free_resources(client);\n    } else {\n        xEventGroupSetBits(client->status_bits, STOPPED_BIT);\n        xSemaphoreGiveRecursive(client->lock);\n    }'
    if websocket_result.count(old_finish) != 1:
        raise ValueError('Managed WebSocket finish publication contract changed; review required')
    websocket_result = websocket_result.replace(old_finish, new_finish)
    start = websocket_result.index('static esp_err_t stop_wait_task(')
    end = websocket_result.index('\n}', start) + 2
    body = websocket_result[start:end]
    wait = '    xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);\n'
    if body.count(wait) != 1:
        raise ValueError('Managed WebSocket stop/join contract changed; review required')
    body = body.replace(wait, wait + '    xSemaphoreTakeRecursive(client->lock, portMAX_DELAY);\n    xSemaphoreGiveRecursive(client->lock);\n')
    websocket_result = websocket_result[:start] + body + websocket_result[end:]
    start = websocket_result.index('esp_err_t esp_websocket_client_destroy(')
    end = websocket_result.index('\n}', start) + 2
    body = websocket_result[start:end]
    last = '    }\n\n    destroy_and_free_resources(client);'
    if body.count(last) != 1:
        raise ValueError('Managed WebSocket destructor barrier contract changed; review required')
    body = body.replace(last, '    } else if (client->lock) {\n        xSemaphoreTakeRecursive(client->lock, portMAX_DELAY);\n        xSemaphoreGiveRecursive(client->lock);\n    }\n\n    destroy_and_free_resources(client);')
    websocket_result = websocket_result[:start] + body + websocket_result[end:]
    # Public external transports may not expose a socket. The diagnostic's
    # bounded close owns teardown; do not bypass it with a raw socket select.
    close_poll = '            int ret = esp_transport_ws_poll_connection_closed(client->transport, 1000);'
    if websocket_result.count(close_poll) != 1:
        raise ValueError('Managed WebSocket close polling contract changed; review required')
    websocket_result = websocket_result.replace(close_poll,
        '            int ret = client->config->ext_transport ? 1 :\n                esp_transport_ws_poll_connection_closed(client->transport, 1000);')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    target = output / 'transport_ssl.c'
    target.write_text(result)
    (output / 'esp_http_client.c').write_text(client_result)
    (output / 'transport_ws.c').write_text(ws_result)
    (output / 'esp_websocket_client.c').write_text(websocket_result)
    metadata = {'sdk_source': str(source_path), 'sdk_sha256': hashlib.sha256(source.encode()).hexdigest(),
                'prepared_sha256': hashlib.sha256(result.encode()).hexdigest(),
                'http_sdk_sha256': hashlib.sha256(client_source.encode()).hexdigest(),
                'http_prepared_sha256': hashlib.sha256(client_result.encode()).hexdigest(),
                'ws_sdk_sha256': hashlib.sha256(ws_source.encode()).hexdigest(),
                'ws_prepared_sha256': hashlib.sha256(ws_result.encode()).hexdigest(),
                'websocket_source': str(websocket_source),
                'websocket_sha256': hashlib.sha256(websocket_original.encode()).hexdigest(),
                'websocket_prepared_sha256': hashlib.sha256(websocket_result.encode()).hexdigest(),
                'websocket_private_diagnostic_sites': len(ws_diagnostics) + len(websocket_diagnostics),
                'private_diagnostic_sites': sum(item[2] for item in private_diagnostics),
                'websocket_start_lifecycle': 'running before task creation;stopped on create failure',
                'websocket_finish_lifecycle': 'client mutex protects STOPPED publication and destructor join',
                'websocket_external_close': 'external transport teardown;no raw socket select',
                'scope': 'nonblocking poll/FIN, private diagnostics, WS lifetime publication and opaque external close; no shared SDK edits'}
    (output / 'inputs.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-dir', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--websocket-source', required=True, type=Path)
    args = parser.parse_args()
    try:
        prepare(args.idf_dir, args.output, args.websocket_source)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')
