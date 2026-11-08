#!/usr/bin/env python3
"""Build-local HTTPD admission and HTTPS resource ownership.

The SDK installation is read-only. Refuse source drift rather than silently
building firmware without the admission policy. Tested with ESP-IDF v5.5.2.
"""
import argparse
from pathlib import Path


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('Unsupported ESP-IDF HTTPD source: admission anchor changed')
    return source.replace(old, new, 1)


def validate_origins(value):
    """Fail configuration before firmware generation; never print header values."""
    import ipaddress
    import re
    from urllib.parse import urlsplit
    if not value:
        return
    entries = value.split(',')
    if len(entries) > 4:
        raise ValueError('At most four explicit trusted origins are supported')
    for entry in entries:
        if not entry or len(entry) >= 288 or any(ord(c) <= 32 or ord(c) >= 127 for c in entry):
            raise ValueError('Trusted origins must be bounded ASCII origins')
        if any(c in entry for c in '@?#%\\*'):
            raise ValueError('Trusted origins cannot contain credentials, escapes or wildcards')
        parsed = urlsplit(entry)
        if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.path or parsed.query or parsed.fragment:
            raise ValueError('Trusted origins must contain only HTTP(S) scheme and authority')
        if parsed.port == 0 or len(parsed.hostname) > 253 or parsed.netloc.endswith(':'):
            raise ValueError('Invalid trusted origin authority or port')
        host = parsed.hostname
        if ':' in host:
            ipaddress.IPv6Address(host)
        elif all(c in '0123456789.' for c in host):
            ipaddress.IPv4Address(host)
        else:
            labels = host[:-1].split('.') if host.endswith('.') else host.split('.')
            if any(not re.fullmatch(r'[a-zA-Z0-9_-]{1,63}', label) for label in labels):
                raise ValueError('Invalid trusted origin DNS labels')


def prepare(idf, output):
    src = Path(idf) / 'components/esp_http_server/src'
    sess = (src / 'httpd_sess.c').read_text()
    sess = replace_once(sess,
        'if (session->for_async_req == false) {',
        '''if (session->for_async_req == false && !session->lru_socket
#ifdef CONFIG_HTTPD_WS_SUPPORT
            && !session->ws_handshake_done
#endif
        ) {''')
    sess = replace_once(sess,
        'if (!context.session) {\n        return ESP_OK;\n    }\n    ESP_LOGD(TAG, LOG_FMT("Closing session with fd %d"), context.session->fd);',
        'if (!context.session) {\n        return ESP_ERR_NOT_FOUND;\n    }\n    ESP_LOGD(TAG, LOG_FMT("Closing session with fd %d"), context.session->fd);')
    sess = replace_once(sess, 'esp_err_t httpd_sess_process(struct httpd_data *hd, struct sock_db *session)',
        'static esp_err_t httpd_sess_process_impl(struct httpd_data *hd, struct sock_db *session)')
    sess += '\n/* Project-owned receive lifetime; every exit, including parse/handler errors,\n * clears the task-local budget before HTTPD processes another socket. */\nextern void ts_httpd_receive_begin(void);\nextern void ts_httpd_receive_end(void);\nesp_err_t httpd_sess_process(struct httpd_data *hd, struct sock_db *session)\n{\n    ts_httpd_receive_begin();\n    esp_err_t ret = httpd_sess_process_impl(hd, session);\n    ts_httpd_receive_end();\n    return ret;\n}\n'
    main = (src / 'httpd_main.c').read_text()
    main = replace_once(main,
        'return httpd_sess_close_lru(hd);',
        '''esp_err_t ret = httpd_sess_close_lru(hd);
            if (ret == ESP_ERR_NOT_FOUND) {
                /* All sessions are protected or closing. Consume the pending
                 * connection and reject it; never spin on a readable listener
                 * or displace an established session. No plaintext is sent on
                 * a potentially TLS listener. Normal WS admission sends 1013
                 * while reserved HTTP capacity is available. */
                int rejected_fd = accept(listen_fd, NULL, NULL);
                if (rejected_fd >= 0) close(rejected_fd);
                return ESP_OK;
            }
            return ret;''')
    uri = (src / 'httpd_uri.c').read_text()
    uri = replace_once(uri, 'esp_err_t httpd_uri(struct httpd_data *hd)\n{',
        'extern esp_err_t ts_httpd_admit_request(httpd_req_t *req);\n\nesp_err_t httpd_uri(struct httpd_data *hd)\n{')
    uri = replace_once(uri, '    httpd_req_t            *req = &hd->hd_req;',
        '    httpd_req_t            *req = &hd->hd_req;\n    if (ts_httpd_admit_request(req) != ESP_OK) return ESP_FAIL;')
    # A registered WebSocket endpoint is not an ordinary GET resource. The
    # application peer owner may run only after SDK upgrade validation/101.
    uri = replace_once(uri, '    struct httpd_req_aux   *aux = req->aux;',
        '    struct httpd_req_aux   *aux = req->aux;\n    if (uri->is_websocket && !aux->ws_handshake_detect) {\n        httpd_req_handle_err(req, HTTPD_400_BAD_REQUEST);\n        return ESP_FAIL; /* Also close when a custom error handler returns OK. */\n    }')
    parse = (src / 'httpd_parse.c').read_text()
    parse = replace_once(parse, 'static esp_err_t cb_headers_complete(http_parser *parser)',
        '/* TS_CONTENT_LENGTH_BEGIN: reject before size_t truncation or URI dispatch. */\nstatic bool ts_httpd_decode_content_length(uint64_t wire_length, bool present, bool chunked, size_t *length)\n{\n    if (chunked) return false; /* SDK does not support chunked request bodies. */\n    if (!present) { *length=0;return true; }\n    /* Wire budget for the current whole-buffer upload handlers.\n     * Authentication and JSON routes select smaller application limits. */\n    if (wire_length>16U*1024U*1024U || wire_length>SIZE_MAX) return false;\n    *length=(size_t)wire_length;\n    return true;\n}\n/* TS_CONTENT_LENGTH_END */\n\nstatic esp_err_t cb_headers_complete(http_parser *parser)')
    parse = replace_once(parse, '    /* In absence of body/chunked encoding, http_parser sets content_len to -1 */\n    r->content_len = ((int)parser->content_length != -1 ?\n                      parser->content_length : 0);', "    /* The presence flag distinguishes absence from an explicit maximum. Casting to int\n     * also mistakes valid low 0xffffffff bits for absence on 32-bit targets. */\n    if (!ts_httpd_decode_content_length(parser->content_length, (parser->flags & F_CONTENTLENGTH)!=0, (parser->flags & F_CHUNKED)!=0, &r->content_len)) {\n        parser_data->error=(parser->flags & F_CHUNKED)?HTTPD_501_METHOD_NOT_IMPLEMENTED:HTTPD_413_CONTENT_TOO_LARGE;\n        parser_data->status=PARSING_FAILED;\n        return ESP_FAIL;\n    }")
    # Validate the private layout owner and lookup anchor instead of changing
    # the globally installed SDK. Header memory is valid only before sending.
    for anchor in ['esp_err_t httpd_req_get_hdr_value_str(', 'ra->scratch_cur_size = 0;', 'ra->req_hdrs_count = 0;']:
        if anchor not in parse: raise ValueError('Unsupported HTTPD header buffer layout')
    parse += r"""

/* Project-only strict security-header access. Duplicate/empty/missing values
 * must not collapse to the first matching value or to an absent Origin. */
esp_err_t ts_httpd_get_single_header(httpd_req_t *r, const char *field,
                                   char *out, size_t capacity)
{
    if (!r || !field || !out || !capacity || !httpd_valid_req(r)) return ESP_ERR_INVALID_ARG;
    out[0] = 0;
    struct httpd_req_aux *ra = r->aux;
    if (!ra || (!ra->scratch && ra->req_hdrs_count)) return ESP_ERR_INVALID_ARG;
    if (!ra->req_hdrs_count) return ESP_ERR_NOT_FOUND;
    const char *at = ra->scratch, *end = at + ra->scratch_cur_size;
    bool found = false;
    for (unsigned i = 0; i < ra->req_hdrs_count; ++i) {
        while (at < end && !*at) ++at;
        if (at == end) return ESP_ERR_INVALID_ARG;
        const char *nul = memchr(at, 0, (size_t)(end - at));
        if (!nul) return ESP_ERR_INVALID_ARG;
        const char *colon = memchr(at, ':', (size_t)(nul - at));
        if (!colon) return ESP_ERR_INVALID_ARG;
        if ((size_t)(colon - at) == strlen(field) && !strncasecmp(at, field, (size_t)(colon - at))) {
            if (found) return ESP_ERR_INVALID_ARG;
            found = true;
            const char *value = colon + 1, *last = nul;
            while (value < last && (*value == ' ' || *value == '\t')) ++value;
            while (last > value && (last[-1] == ' ' || last[-1] == '\t')) --last;
            size_t length = (size_t)(last - value);
            if (length >= capacity) return ESP_ERR_HTTPD_RESULT_TRUNC;
            memcpy(out, value, length); out[length] = 0;
        }
        at = nul + 1;
    }
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}
"""

    # Diagnostics feed the shared log ring, including when SDK DEBUG or purge
    # logging is enabled. Keep protocol bytes in their owner, never in logs.
    txrx = (src / 'httpd_txrx.c').read_text()
    ws = (src / 'httpd_ws.c').read_text()
    ws = replace_once(ws, '            frame->len = (((uint64_t)length_bytes[0] << 56U) |\n                    ((uint64_t)length_bytes[1] << 48U) |\n                    ((uint64_t)length_bytes[2] << 40U) |\n                    ((uint64_t)length_bytes[3] << 32U) |\n                    ((uint64_t)length_bytes[4] << 24U) |\n                    ((uint64_t)length_bytes[5] << 16U) |\n                    ((uint64_t)length_bytes[6] <<  8U) |\n                    ((uint64_t)length_bytes[7]));', '            uint64_t wide_len = (((uint64_t)length_bytes[0] << 56U) |\n                    ((uint64_t)length_bytes[1] << 48U) |\n                    ((uint64_t)length_bytes[2] << 40U) |\n                    ((uint64_t)length_bytes[3] << 32U) |\n                    ((uint64_t)length_bytes[4] << 24U) |\n                    ((uint64_t)length_bytes[5] << 16U) |\n                    ((uint64_t)length_bytes[6] <<  8U) |\n                    ((uint64_t)length_bytes[7]));\n            if (wide_len > SIZE_MAX || wide_len > INT64_MAX) {\n                ESP_LOGW(TAG, "WS wire length is not representable");\n                return ESP_ERR_INVALID_SIZE;\n            }\n            frame->len = (size_t)wide_len;')
    txrx = replace_once(txrx, 'static const char *TAG = "httpd_txrx";',
        '/* TS_RX_BUDGET_BEGIN: task-local, monotonic and independent per listener. */\nstatic _Thread_local uint32_t s_receive_started, s_receive_budget;\nstatic _Thread_local bool s_receive_active, s_receive_body_started;\nstatic bool ts_httpd_receive_expired(void)\n{\n    return s_receive_active && (uint32_t)(esp_log_timestamp()-s_receive_started)>=s_receive_budget;\n}\nvoid ts_httpd_receive_begin(void)\n{\n    s_receive_started=esp_log_timestamp();s_receive_budget=15000;\n    s_receive_active=true;s_receive_body_started=false;\n}\nvoid ts_httpd_receive_end(void)\n{\n    s_receive_active=false;s_receive_body_started=false;s_receive_budget=0;\n}\nvoid ts_httpd_set_body_receive_budget(uint32_t milliseconds)\n{\n    if (!s_receive_active || s_receive_body_started) return;\n    if (!milliseconds) milliseconds=30000;\n    if (milliseconds>300000) milliseconds=300000;\n    s_receive_started=esp_log_timestamp();s_receive_budget=milliseconds;\n    s_receive_body_started=true;\n}\n/* TS_RX_BUDGET_END */\n\nstatic const char *TAG = "httpd_txrx";')
    txrx = replace_once(txrx,
        'int httpd_recv_with_opt(httpd_req_t *r, char *buf, size_t buf_len, httpd_recv_opt_t opt)\n{',
        'int httpd_recv_with_opt(httpd_req_t *r, char *buf, size_t buf_len, httpd_recv_opt_t opt)\n{\n    if (ts_httpd_receive_expired()) return HTTPD_SOCK_ERR_TIMEOUT;')
    txrx = replace_once(txrx,
        'int ret = ra->sd->recv_fn(ra->sd->handle, ra->sd->fd, buf, buf_len, 0);',
        'int ret = ra->sd->recv_fn(ra->sd->handle, ra->sd->fd, buf, buf_len, 0);\n        if (ts_httpd_receive_expired()) return HTTPD_SOCK_ERR_TIMEOUT;\n        if (ret == 0) return recv_len; /* EOF must not spin in blocking mode. */')
    parse = replace_once(parse, 'ESP_LOGD(TAG, LOG_FMT("received URI = %s"), r->uri);', 'ESP_LOGD(TAG, LOG_FMT("received URI (%"NEWLIB_NANO_COMPAT_FORMAT" bytes)"), NEWLIB_NANO_COMPAT_CAST(length));')
    parse = replace_once(parse, 'ESP_LOGD(TAG, LOG_FMT("processing url = %.*s"), (int)length, at);', 'ESP_LOGD(TAG, LOG_FMT("processing URL fragment (%"NEWLIB_NANO_COMPAT_FORMAT" bytes)"), NEWLIB_NANO_COMPAT_CAST(length));')
    parse = replace_once(parse, 'ESP_LOGD(TAG, LOG_FMT("processing field = %.*s"), (int)length, at);', 'ESP_LOGD(TAG, LOG_FMT("processing header name (%"NEWLIB_NANO_COMPAT_FORMAT" bytes)"), NEWLIB_NANO_COMPAT_CAST(length));')
    parse = replace_once(parse, 'ESP_LOGD(TAG, LOG_FMT("processing value = %.*s"), (int)length, at);', 'ESP_LOGD(TAG, LOG_FMT("processing header value (%"NEWLIB_NANO_COMPAT_FORMAT" bytes)"), NEWLIB_NANO_COMPAT_CAST(length));')
    parse = replace_once(parse, 'ESP_LOGW(TAG, LOG_FMT("Upgrade header found but it\'s %s"), ws_upgrade_hdr_val);', 'ESP_LOGW(TAG, LOG_FMT("Unsupported Upgrade header value"));')
    parse = replace_once(parse, 'ESP_LOG_BUFFER_HEX_LEVEL(TAG, dummy, recv_len, ESP_LOG_DEBUG);', 'ESP_LOGD(TAG, LOG_FMT("purged payload omitted"));')
    parse = replace_once(parse, 'ESP_LOGD(TAG, LOG_FMT("key %s not found"), key);', 'ESP_LOGD(TAG, LOG_FMT("header key not found"));')
    parse = replace_once(parse, 'ESP_LOGD(TAG, LOG_FMT("cookie %s not found"), key);', 'ESP_LOGD(TAG, LOG_FMT("cookie key not found"));')
    parse = replace_once(parse, 'ESP_LOGW(TAG, "Cookie not found in header uri:[%s]", req->uri);', 'ESP_LOGW(TAG, "Cookie not found in request header");')
    uri = replace_once(uri, 'ESP_LOGD(TAG, LOG_FMT("request for %s with type %d"), req->uri, req->method);', 'ESP_LOGD(TAG, LOG_FMT("request method %d"), req->method);')
    uri = replace_once(uri, 'ESP_LOGW(TAG, LOG_FMT("URI \'%s\' not found"), req->uri);', 'ESP_LOGW(TAG, LOG_FMT("request route not found"));')
    uri = replace_once(uri, 'ESP_LOGW(TAG, LOG_FMT("Method \'%d\' not allowed for URI \'%s\'"),\n                         req->method, req->uri);', 'ESP_LOGW(TAG, LOG_FMT("Method %d not allowed for request route"), req->method);')
    ws = replace_once(ws, 'ESP_LOGW(TAG, "Sec-WebSocket-Protocol %s not supported, URI do not support any subprotocols", subprotocol);', 'ESP_LOGW(TAG, "Requested WebSocket subprotocol unsupported: route has none");')
    ws = replace_once(ws, 'ESP_LOGD(TAG, "Requested subprotocol supported: %s", s);', 'ESP_LOGD(TAG, "Requested WebSocket subprotocol supported");')
    ws = replace_once(ws, 'ESP_LOGW(TAG, "Sec-WebSocket-Protocol %s not supported, supported subprotocol is %s", subprotocol, supported_subprotocol);', 'ESP_LOGW(TAG, "Requested WebSocket subprotocol unsupported");')
    ws = replace_once(ws, 'ESP_LOGW(TAG, LOG_FMT("\\"Sec-WebSocket-Version\\" is not \\"13\\", it is: %s"), version_val);', 'ESP_LOGW(TAG, LOG_FMT("Unsupported WebSocket version"));')
    ws = replace_once(ws, 'ESP_LOGD(TAG, LOG_FMT("Server key before encoding: %s"), server_raw_text);', 'ESP_LOGD(TAG, LOG_FMT("WebSocket handshake digest prepared"));')
    ws = replace_once(ws, 'ESP_LOGD(TAG, LOG_FMT("Generated server key: %s"), server_key_encoded);', 'ESP_LOGD(TAG, LOG_FMT("WebSocket handshake digest encoded"));')
    ws = replace_once(ws, 'ESP_LOGD(TAG, "subprotocol: %s", subprotocol);', 'ESP_LOGD(TAG, "WebSocket response subprotocol selected");')
    txrx = replace_once(txrx, 'ESP_LOGD(TAG, LOG_FMT("new header = %s: %s"), field, value);',
        'ESP_LOGD(TAG, LOG_FMT("response header stored"));')
    error_log = 'ESP_LOGW(TAG, LOG_FMT("%s - %s"), status, msg);'
    if txrx.count(error_log) != 2:
        raise ValueError('Unsupported HTTPD response error diagnostics')
    txrx = txrx.replace(error_log, 'ESP_LOGW(TAG, LOG_FMT("HTTP error response"));')

    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    for name, content in [('httpd_sess.c', sess), ('httpd_main.c', main), ('httpd_uri.c', uri), ('httpd_parse.c', parse), ('httpd_txrx.c', txrx), ('httpd_ws.c', ws)]:
        dest = output / name
        if not dest.exists() or dest.read_text() != content:
            dest.write_text(content)


def prepare_https(idf, output):
    """Keep one owner for copied TLS configuration on partial/startup failure."""
    source = (Path(idf) / 'components/esp_https_server/src/https_server.c').read_text()
    old = """    esp_tls_cfg_server_t *cfg = ssl_ctx->tls_cfg;
    ESP_LOGI(TAG, "Server shuts down, releasing SSL context");
    if (cfg->cacert_buf) {
        free((void *)cfg->cacert_buf);
    }
    if (cfg->servercert_buf) {
        free((void *)cfg->servercert_buf);
    }
    if (cfg->serverkey_buf) {
        free((void *)cfg->serverkey_buf);
    }
    esp_tls_cfg_server_session_tickets_free(cfg);
    free(cfg);
    free(ssl_ctx);"""
    source = replace_once(source, old, """    ts_httpd_free_tls_config(ssl_ctx->tls_cfg);
    free(ssl_ctx);""")
    marker = 'static void free_secure_context(void *ctx)'
    source = replace_once(source, marker, """/* Copied PEM/ticket material has one cleanup owner, including partial init. */
static void ts_httpd_free_tls_config(esp_tls_cfg_server_t *cfg)
{
    if (!cfg) return;
    free((void *)cfg->cacert_buf);
    free((void *)cfg->servercert_buf);
    free((void *)cfg->serverkey_buf);
    esp_tls_cfg_server_session_tickets_free(cfg);
    free(cfg);
}

""" + marker)
    source = replace_once(source, """exit:
    if (cfg) {
        free((void *) cfg->servercert_buf);
        free((void *) cfg->cacert_buf);
    }
    free(cfg);
    return ret;""", """exit:
    ts_httpd_free_tls_config(cfg);
    return ret;""")
    source = replace_once(source, """    ret = httpd_start(&handle, &config->httpd);
    if (ret != ESP_OK) {
        free(ssl_ctx);
        ssl_ctx = NULL;
        return ret;
    }""", """    ret = httpd_start(&handle, &config->httpd);
    if (ret != ESP_OK) {
        if (ssl_ctx) free_secure_context(ssl_ctx);
        return ret;
    }""")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    dest = output / 'https_server.c'
    if not dest.exists() or dest.read_text() != source:
        dest.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-dir', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--trusted-origins', default='')
    parser.add_argument('--with-https', action='store_true')
    args = parser.parse_args()
    validate_origins(args.trusted_origins)
    prepare(args.idf_dir, args.output)
    if args.with_https:
        prepare_https(args.idf_dir, args.output)
