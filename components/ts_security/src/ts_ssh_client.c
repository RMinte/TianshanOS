/**
 * @file ts_ssh_client.c
 * @brief SSH Client implementation using libssh2
 *
 * This module provides SSH client functionality for TianShanOS,
 * using libssh2 library for full SSH2 protocol support.
 * 
 * 会话结构和缓冲区优先分配到 PSRAM
 */

#include "ts_ssh_client.h"
#include "ts_core.h"  /* TS_MALLOC_PSRAM, TS_CALLOC_PSRAM */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdarg.h>

static const char *TAG = "ts_ssh";

/* ============================================================================
 * 内部数据结构
 * ============================================================================ */

/** SSH 会话内部结构 */
struct ts_ssh_session_s {
    ts_ssh_config_t config;          /**< 会话配置 */
    ts_ssh_state_t state;            /**< 当前状态 */
    int sock;                        /**< Socket 文件描述符 */
    LIBSSH2_SESSION *session;        /**< libssh2 会话句柄 */
    char error_msg[256];             /**< 最后的错误消息 */
    char *host_copy;                 /**< 主机地址副本 */
    char *username_copy;             /**< 用户名副本 */
    char *password_copy;             /**< 密码副本 */
    volatile bool abort_flag;        /**< 中止标志 */
};

/** 全局初始化标志 */
static bool s_initialized = false;

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 等待 socket 可读/可写
 */
static int wait_socket(int sock, LIBSSH2_SESSION *session, int timeout_ms)
{
    struct timeval timeout;
    fd_set fd;
    fd_set *readfd = NULL;
    fd_set *writefd = NULL;
    int dir;

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(&fd);
    FD_SET(sock, &fd);

    /* 获取 libssh2 需要等待的方向 */
    dir = libssh2_session_block_directions(session);

    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) {
        readfd = &fd;
    }

    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
        writefd = &fd;
    }

    return select(sock + 1, readfd, writefd, NULL, &timeout);
}

/**
 * @brief 设置错误消息
 */
static void set_error(ts_ssh_session_t session, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(session->error_msg, sizeof(session->error_msg), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s", session->error_msg);
}

/**
 * @brief 复制字符串
 */
static char *strdup_safe(const char *str)
{
    if (str == NULL) {
        return NULL;
    }
    char *copy = TS_MALLOC_PSRAM(strlen(str) + 1);
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

/* ============================================================================
 * 公开 API 实现
 * ============================================================================ */

esp_err_t ts_ssh_client_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    int rc = libssh2_init(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "libssh2_init failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SSH client initialized (libssh2 version: %s)", LIBSSH2_VERSION);
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ts_ssh_client_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    libssh2_exit();
    s_initialized = false;
    ESP_LOGI(TAG, "SSH client deinitialized");
    return ESP_OK;
}

esp_err_t ts_ssh_session_create(const ts_ssh_config_t *config, ts_ssh_session_t *session_out)
{
    if (!config || !session_out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->host || !config->username) {
        ESP_LOGE(TAG, "Host and username are required");
        return ESP_ERR_INVALID_ARG;
    }

    /* 确保全局初始化 */
    if (!s_initialized) {
        esp_err_t ret = ts_ssh_client_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    /* 分配会话结构 */
    ts_ssh_session_t session = TS_CALLOC_PSRAM(1, sizeof(struct ts_ssh_session_s));
    if (!session) {
        return ESP_ERR_NO_MEM;
    }

    /* 复制配置 */
    session->config = *config;
    session->config.port = config->port ? config->port : 22;
    session->config.timeout_ms = config->timeout_ms ? config->timeout_ms : 10000;
    
    /* 复制字符串（因为调用者可能释放原始字符串） */
    session->host_copy = strdup_safe(config->host);
    session->username_copy = strdup_safe(config->username);
    session->config.host = session->host_copy;
    session->config.username = session->username_copy;

    if (config->auth_method == TS_SSH_AUTH_PASSWORD && config->auth.password) {
        session->password_copy = strdup_safe(config->auth.password);
        session->config.auth.password = session->password_copy;
    }

    session->sock = -1;
    session->session = NULL;
    session->state = TS_SSH_STATE_DISCONNECTED;
    session->error_msg[0] = '\0';

    *session_out = session;
    ESP_LOGD(TAG, "Session created for %s@%s:%d", 
             session->config.username, session->config.host, session->config.port);
    return ESP_OK;
}

esp_err_t ts_ssh_session_destroy(ts_ssh_session_t session)
{
    if (!session) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 先断开连接 */
    if (session->state != TS_SSH_STATE_DISCONNECTED) {
        ts_ssh_disconnect(session);
    }

    /* 释放字符串副本 */
    free(session->host_copy);
    free(session->username_copy);
    free(session->password_copy);

    /* 释放会话结构 */
    free(session);
    return ESP_OK;
}

esp_err_t ts_ssh_connect(ts_ssh_session_t session)
{
    if (!session) {
        return ESP_ERR_INVALID_ARG;
    }

    if (session->state == TS_SSH_STATE_CONNECTED) {
        return ESP_OK;  /* 已连接 */
    }

    session->state = TS_SSH_STATE_CONNECTING;

    /* 解析主机地址 */
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(session->config.port);
    sin.sin_addr.s_addr = inet_addr(session->config.host);

    /* 如果不是有效的 IP 地址，尝试 DNS 解析 */
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *hp = gethostbyname(session->config.host);
        if (hp == NULL) {
            set_error(session, "Failed to resolve host: %s", session->config.host);
            session->state = TS_SSH_STATE_ERROR;
            return ESP_FAIL;
        }
        memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
    }

    /* 创建 socket */
    session->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (session->sock < 0) {
        set_error(session, "Failed to create socket: %s", strerror(errno));
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }

    /* 设置 socket 超时 */
    struct timeval tv;
    tv.tv_sec = session->config.timeout_ms / 1000;
    tv.tv_usec = (session->config.timeout_ms % 1000) * 1000;
    setsockopt(session->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(session->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* 连接 */
    ESP_LOGI(TAG, "Connecting to %s:%d...", session->config.host, session->config.port);
    if (connect(session->sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        set_error(session, "Failed to connect: %s", strerror(errno));
        close(session->sock);
        session->sock = -1;
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }

    /* 创建 libssh2 会话 */
    session->session = libssh2_session_init();
    if (!session->session) {
        set_error(session, "Failed to create SSH session");
        close(session->sock);
        session->sock = -1;
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }

    /* 设置非阻塞模式 */
    libssh2_session_set_blocking(session->session, 0);

    /* 执行 SSH 握手 */
    int rc;
    while ((rc = libssh2_session_handshake(session->session, session->sock)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(session->sock, session->session, 1000);
    }

    if (rc != 0) {
        char *err_msg = NULL;
        libssh2_session_last_error(session->session, &err_msg, NULL, 0);
        set_error(session, "SSH handshake failed: %s (rc=%d)", err_msg ? err_msg : "unknown", rc);
        libssh2_session_free(session->session);
        session->session = NULL;
        close(session->sock);
        session->sock = -1;
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "SSH handshake completed");
    session->state = TS_SSH_STATE_AUTHENTICATING;

    /* 执行认证 */
    if (session->config.auth_method == TS_SSH_AUTH_PASSWORD) {
        /* 密码认证 */
        ESP_LOGD(TAG, "Authenticating with password...");
        while ((rc = libssh2_userauth_password(session->session, 
                                                session->config.username,
                                                session->config.auth.password)) == LIBSSH2_ERROR_EAGAIN) {
            wait_socket(session->sock, session->session, 1000);
        }
    } else {
        /* 公钥认证 */
        
        /* 首先检查服务器支持的认证方法（需要处理 EAGAIN） */
        char *userauthlist = NULL;
        do {
            userauthlist = libssh2_userauth_list(session->session, 
                                                  session->config.username,
                                                  strlen(session->config.username));
            if (!userauthlist) {
                int err = libssh2_session_last_errno(session->session);
                if (err == LIBSSH2_ERROR_EAGAIN) {
                    wait_socket(session->sock, session->session, 1000);
                    continue;
                }
                /* 其他错误或已认证 */
                break;
            }
        } while (!userauthlist);
        
        if (userauthlist) {
            ESP_LOGI(TAG, "Server authentication methods: %s", userauthlist);
            if (!strstr(userauthlist, "publickey")) {
                ESP_LOGW(TAG, "Server does not support publickey authentication!");
            }
        }
        
        /* 优先使用文件路径方式（更可靠） */
        if (session->config.auth.key.private_key_path) {
            const char *key_path = session->config.auth.key.private_key_path;
            ESP_LOGI(TAG, "Authenticating with public key from file: %s", key_path);
            
            /* 验证文件存在且可读 */
            FILE *f = fopen(key_path, "r");
            if (!f) {
                ESP_LOGE(TAG, "Cannot open private key file: %s (errno=%d)", key_path, errno);
                set_error(session, "Cannot open private key file");
                libssh2_session_disconnect(session->session, "Key file error");
                libssh2_session_free(session->session);
                session->session = NULL;
                close(session->sock);
                session->sock = -1;
                session->state = TS_SSH_STATE_ERROR;
                return ESP_ERR_NOT_FOUND;
            }
            
            /* 读取并打印前 64 字节用于调试 */
            char header[65] = {0};
            size_t read_len = fread(header, 1, 64, f);
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            fclose(f);
            (void)read_len;  /* 抑制未使用警告 */
            
            ESP_LOGI(TAG, "Key file size: %ld bytes, header: %.40s...", file_size, header);
            
            /* 构造公钥文件路径 (私钥路径 + ".pub") */
            char pubkey_path[128];
            snprintf(pubkey_path, sizeof(pubkey_path), "%s.pub", key_path);
            
            /* 检查公钥文件是否存在 */
            const char *pubkey_ptr = NULL;
            FILE *pub_f = fopen(pubkey_path, "r");
            if (pub_f) {
                fclose(pub_f);
                pubkey_ptr = pubkey_path;
                ESP_LOGI(TAG, "Using public key file: %s", pubkey_path);
            } else {
                ESP_LOGW(TAG, "Public key file not found: %s (will derive from private key)", pubkey_path);
            }
            
            /* 清除之前的错误状态 */
            libssh2_session_set_last_error(session->session, 0, NULL);
            
            while ((rc = libssh2_userauth_publickey_fromfile_ex(
                        session->session,
                        session->config.username, strlen(session->config.username),
                        pubkey_ptr,  /* 公钥路径（如果存在） */
                        key_path,
                        session->config.auth.key.passphrase)) == LIBSSH2_ERROR_EAGAIN) {
                wait_socket(session->sock, session->session, 1000);
            }
            
            if (rc != 0) {
                char *err_msg = NULL;
                int err_len = 0;
                int err_code = libssh2_session_last_error(session->session, &err_msg, &err_len, 0);
                ESP_LOGE(TAG, "libssh2_userauth_publickey_fromfile_ex failed: rc=%d, err_code=%d, err=%s", 
                         rc, err_code, err_msg ? err_msg : "(null)");
                
                /* 尝试使用 libssh2_userauth_publickey 回调方式 */
                ESP_LOGI(TAG, "Trying alternative authentication method...");
            }
        } 
        /* 回退到内存方式 */
        else if (session->config.auth.key.private_key && 
                 session->config.auth.key.private_key_len > 0) {
            ESP_LOGI(TAG, "Authenticating with public key from memory (key_len=%zu)...", 
                     session->config.auth.key.private_key_len);
            
            /* 打印私钥头部用于调试 */
            const char *key_data = (const char *)session->config.auth.key.private_key;
            size_t key_len = session->config.auth.key.private_key_len;
            ESP_LOGI(TAG, "Private key header: %.50s", key_data);
            ESP_LOGI(TAG, "Private key length: %zu", key_len);
            
            /* 检查 PEM 格式 */
            if (strstr(key_data, "-----BEGIN RSA PRIVATE KEY-----")) {
                ESP_LOGI(TAG, "Key type: RSA (PKCS#1)");
            } else if (strstr(key_data, "-----BEGIN EC PRIVATE KEY-----")) {
                ESP_LOGI(TAG, "Key type: EC (ECDSA) - Note: libssh2 mbedTLS backend may not support ECDSA frommemory");
            } else if (strstr(key_data, "-----BEGIN PRIVATE KEY-----")) {
                ESP_LOGI(TAG, "Key type: PKCS#8 - May require conversion to PKCS#1");
            } else if (strstr(key_data, "-----BEGIN OPENSSH PRIVATE KEY-----")) {
                ESP_LOGE(TAG, "Key type: OpenSSH - NOT SUPPORTED! Please use PEM format (ssh-keygen -m PEM)");
            } else {
                ESP_LOGW(TAG, "Key type: Unknown format");
            }
            
            /* 
             * libssh2_userauth_publickey_frommemory 内部需要获取认证方法列表
             * 如果之前没有调用过 libssh2_userauth_list，需要先调用一次
             * 确保 session 处于正确的状态
             */
            if (!userauthlist) {
                do {
                    userauthlist = libssh2_userauth_list(session->session, 
                                                          session->config.username,
                                                          strlen(session->config.username));
                    if (!userauthlist) {
                        int err = libssh2_session_last_errno(session->session);
                        if (err == LIBSSH2_ERROR_EAGAIN) {
                            wait_socket(session->sock, session->session, 1000);
                            continue;
                        }
                        break;
                    }
                } while (!userauthlist);
                
                if (userauthlist) {
                    ESP_LOGI(TAG, "Server authentication methods: %s", userauthlist);
                }
            }
            
            /* 清除之前的错误状态 */
            libssh2_session_set_last_error(session->session, 0, NULL);
            
            while ((rc = libssh2_userauth_publickey_frommemory(
                        session->session,
                        session->config.username, strlen(session->config.username),
                        NULL, 0,  /* 公钥数据（可选） */
                        (const char *)session->config.auth.key.private_key,
                        session->config.auth.key.private_key_len,
                        session->config.auth.key.passphrase)) == LIBSSH2_ERROR_EAGAIN) {
                wait_socket(session->sock, session->session, 1000);
            }
            
            if (rc != 0) {
                char *err_msg = NULL;
                int err_len = 0;
                int err_code = libssh2_session_last_error(session->session, &err_msg, &err_len, 0);
                ESP_LOGE(TAG, "libssh2_userauth_publickey_frommemory failed:");
                ESP_LOGE(TAG, "  rc=%d, err_code=%d, err_msg=%s", 
                         rc, err_code, err_msg ? err_msg : "(null)");
                
                /* 提供更友好的错误提示 */
                if (rc == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED) {
                    ESP_LOGE(TAG, "  Hint: Server rejected the public key. Check if key is authorized on server.");
                } else if (rc == LIBSSH2_ERROR_FILE) {
                    ESP_LOGE(TAG, "  Hint: Key format error. Make sure key is in PEM format (not OpenSSH format).");
                } else if (rc == LIBSSH2_ERROR_METHOD_NONE) {
                    ESP_LOGE(TAG, "  Hint: No handler for this key type. Try RSA key instead of ECDSA.");
                } else if (rc == -1 && (!err_msg || strlen(err_msg) == 0)) {
                    ESP_LOGE(TAG, "  Hint: Unknown error (-1). Possible causes:");
                    ESP_LOGE(TAG, "    1. Key format not supported (try RSA instead of ECDSA)");
                    ESP_LOGE(TAG, "    2. Key parsing failed (check PEM format)");
                    ESP_LOGE(TAG, "    3. mbedTLS backend limitation");
                }
            }
        } else {
            set_error(session, "No private key provided (path or memory)");
            libssh2_session_disconnect(session->session, "No key data");
            libssh2_session_free(session->session);
            session->session = NULL;
            close(session->sock);
            session->sock = -1;
            session->state = TS_SSH_STATE_ERROR;
            return ESP_ERR_INVALID_ARG;
        }
    }

    if (rc != 0) {
        char *err_msg = NULL;
        libssh2_session_last_error(session->session, &err_msg, NULL, 0);
        set_error(session, "Authentication failed: %s (rc=%d)", err_msg ? err_msg : "unknown", rc);
        libssh2_session_disconnect(session->session, "Authentication failed");
        libssh2_session_free(session->session);
        session->session = NULL;
        close(session->sock);
        session->sock = -1;
        session->state = TS_SSH_STATE_ERROR;
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Connected and authenticated to %s@%s", 
             session->config.username, session->config.host);
    session->state = TS_SSH_STATE_CONNECTED;
    return ESP_OK;
}

esp_err_t ts_ssh_disconnect(ts_ssh_session_t session)
{
    if (!session) {
        return ESP_ERR_INVALID_ARG;
    }

    if (session->session) {
        libssh2_session_disconnect(session->session, "Normal shutdown");
        libssh2_session_free(session->session);
        session->session = NULL;
    }

    if (session->sock >= 0) {
        close(session->sock);
        session->sock = -1;
    }

    session->state = TS_SSH_STATE_DISCONNECTED;
    ESP_LOGD(TAG, "Disconnected");
    return ESP_OK;
}

bool ts_ssh_is_connected(ts_ssh_session_t session)
{
    return session && session->state == TS_SSH_STATE_CONNECTED;
}

ts_ssh_state_t ts_ssh_get_state(ts_ssh_session_t session)
{
    return session ? session->state : TS_SSH_STATE_DISCONNECTED;
}

esp_err_t ts_ssh_exec(ts_ssh_session_t session, const char *command, ts_ssh_exec_result_t *result)
{
    if (!session || !command || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    if (session->state != TS_SSH_STATE_CONNECTED) {
        set_error(session, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }

    memset(result, 0, sizeof(ts_ssh_exec_result_t));
    result->exit_code = -1;

    /* 打开通道 */
    LIBSSH2_CHANNEL *channel = NULL;
    int rc;

    while ((channel = libssh2_channel_open_session(session->session)) == NULL &&
           libssh2_session_last_error(session->session, NULL, NULL, 0) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(session->sock, session->session, 1000);
    }

    if (!channel) {
        char *err_msg = NULL;
        libssh2_session_last_error(session->session, &err_msg, NULL, 0);
        set_error(session, "Failed to open channel: %s", err_msg ? err_msg : "unknown");
        return ESP_FAIL;
    }

    /* 执行命令 */
    ESP_LOGD(TAG, "Executing: %s", command);
    while ((rc = libssh2_channel_exec(channel, command)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(session->sock, session->session, 1000);
    }

    if (rc != 0) {
        set_error(session, "Failed to exec command: rc=%d", rc);
        libssh2_channel_free(channel);
        return ESP_FAIL;
    }

    /* 读取输出 */
    size_t stdout_capacity = 4096;
    size_t stderr_capacity = 1024;
    result->stdout_data = TS_MALLOC_PSRAM(stdout_capacity);
    result->stderr_data = TS_MALLOC_PSRAM(stderr_capacity);
    
    if (!result->stdout_data || !result->stderr_data) {
        free(result->stdout_data);
        free(result->stderr_data);
        result->stdout_data = NULL;
        result->stderr_data = NULL;
        libssh2_channel_free(channel);
        return ESP_ERR_NO_MEM;
    }

    result->stdout_len = 0;
    result->stderr_len = 0;

    /* 循环读取直到完成 */
    for (;;) {
        char buffer[512];
        
        /* 读取 stdout */
        do {
            rc = libssh2_channel_read(channel, buffer, sizeof(buffer));
            if (rc > 0) {
                /* 扩展缓冲区 */
                while (result->stdout_len + rc >= stdout_capacity) {
                    stdout_capacity *= 2;
                    char *new_buf = TS_REALLOC_PSRAM(result->stdout_data, stdout_capacity);
                    if (!new_buf) {
                        set_error(session, "Out of memory");
                        ts_ssh_exec_result_free(result);
                        libssh2_channel_free(channel);
                        return ESP_ERR_NO_MEM;
                    }
                    result->stdout_data = new_buf;
                }
                memcpy(result->stdout_data + result->stdout_len, buffer, rc);
                result->stdout_len += rc;
            }
        } while (rc > 0);

        /* 读取 stderr */
        do {
            rc = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
            if (rc > 0) {
                /* 扩展缓冲区 */
                while (result->stderr_len + rc >= stderr_capacity) {
                    stderr_capacity *= 2;
                    char *new_buf = TS_REALLOC_PSRAM(result->stderr_data, stderr_capacity);
                    if (!new_buf) {
                        set_error(session, "Out of memory");
                        ts_ssh_exec_result_free(result);
                        libssh2_channel_free(channel);
                        return ESP_ERR_NO_MEM;
                    }
                    result->stderr_data = new_buf;
                }
                memcpy(result->stderr_data + result->stderr_len, buffer, rc);
                result->stderr_len += rc;
            }
        } while (rc > 0);

        /* 检查是否完成 */
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            wait_socket(session->sock, session->session, 1000);
        } else {
            break;
        }
    }

    /* 添加字符串终止符 */
    result->stdout_data[result->stdout_len] = '\0';
    result->stderr_data[result->stderr_len] = '\0';

    /* 关闭通道并获取退出码 */
    while ((rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(session->sock, session->session, 1000);
    }

    if (rc == 0) {
        result->exit_code = libssh2_channel_get_exit_status(channel);
    }

    libssh2_channel_free(channel);

    ESP_LOGD(TAG, "Command completed: exit_code=%d, stdout_len=%zu, stderr_len=%zu",
             result->exit_code, result->stdout_len, result->stderr_len);
    return ESP_OK;
}

esp_err_t ts_ssh_exec_stream(ts_ssh_session_t session, const char *command,
                              ts_ssh_output_cb_t callback, void *user_data,
                              int *exit_code)
{
    if (!session || !command || !callback) {
        return ESP_ERR_INVALID_ARG;
    }

    if (session->state != TS_SSH_STATE_CONNECTED) {
        set_error(session, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }

    /* 清除中止标志 */
    session->abort_flag = false;

    /* 打开通道 */
    LIBSSH2_CHANNEL *channel = NULL;
    int rc;

    while ((channel = libssh2_channel_open_session(session->session)) == NULL &&
           libssh2_session_last_error(session->session, NULL, NULL, 0) == LIBSSH2_ERROR_EAGAIN) {
        if (session->abort_flag) {
            set_error(session, "Aborted by user");
            return ESP_ERR_TIMEOUT;
        }
        wait_socket(session->sock, session->session, 1000);
    }

    if (!channel) {
        char *err_msg = NULL;
        libssh2_session_last_error(session->session, &err_msg, NULL, 0);
        set_error(session, "Failed to open channel: %s", err_msg ? err_msg : "unknown");
        return ESP_FAIL;
    }

    /* 执行命令 */
    while ((rc = libssh2_channel_exec(channel, command)) == LIBSSH2_ERROR_EAGAIN) {
        if (session->abort_flag) {
            libssh2_channel_free(channel);
            set_error(session, "Aborted by user");
            return ESP_ERR_TIMEOUT;
        }
        wait_socket(session->sock, session->session, 1000);
    }

    if (rc != 0) {
        set_error(session, "Failed to exec command: rc=%d", rc);
        libssh2_channel_free(channel);
        return ESP_FAIL;
    }

    /* 流式读取输出 */
    char buffer[512];
    bool aborted = false;
    for (;;) {
        /* 检查中止标志 */
        if (session->abort_flag) {
            aborted = true;
            break;
        }

        /* 读取 stdout */
        do {
            rc = libssh2_channel_read(channel, buffer, sizeof(buffer));
            if (rc > 0) {
                callback(buffer, rc, false, user_data);
            }
            if (session->abort_flag) {
                aborted = true;
                break;
            }
        } while (rc > 0);

        if (aborted) break;

        /* 读取 stderr */
        do {
            rc = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
            if (rc > 0) {
                callback(buffer, rc, true, user_data);
            }
            if (session->abort_flag) {
                aborted = true;
                break;
            }
        } while (rc > 0);

        if (aborted) break;

        /* 检查 channel 是否已经结束（远程命令退出）*/
        if (libssh2_channel_eof(channel)) {
            break;  /* 命令已完成，退出循环 */
        }

        if (rc == LIBSSH2_ERROR_EAGAIN) {
            wait_socket(session->sock, session->session, 100);  /* 减少等待时间以便更快响应中断 */
        } else if (rc == 0) {
            /* 没有数据但 channel 未结束，短暂等待后继续 */
            wait_socket(session->sock, session->session, 50);
        } else {
            break;
        }
    }

    /* 关闭通道 */
    while ((rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN) {
        if (session->abort_flag) break;  /* 中止时也要快速退出 */
        wait_socket(session->sock, session->session, 100);
    }

    if (exit_code) {
        *exit_code = aborted ? -1 : ((rc == 0) ? libssh2_channel_get_exit_status(channel) : -1);
    }

    libssh2_channel_free(channel);
    
    if (aborted) {
        set_error(session, "Aborted by user");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void ts_ssh_abort(ts_ssh_session_t session)
{
    if (session) {
        session->abort_flag = true;
    }
}

void ts_ssh_exec_result_free(ts_ssh_exec_result_t *result)
{
    if (result) {
        free(result->stdout_data);
        free(result->stderr_data);
        result->stdout_data = NULL;
        result->stderr_data = NULL;
        result->stdout_len = 0;
        result->stderr_len = 0;
    }
}

const char *ts_ssh_get_error(ts_ssh_session_t session)
{
    return session ? session->error_msg : "Invalid session";
}

esp_err_t ts_ssh_exec_simple(const ts_ssh_config_t *config, const char *command,
                              ts_ssh_exec_result_t *result)
{
    ts_ssh_session_t session = NULL;
    esp_err_t ret;

    ret = ts_ssh_session_create(config, &session);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_ssh_session_destroy(session);
        return ret;
    }

    ret = ts_ssh_exec(session, command, result);
    
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    return ret;
}

/* ============================================================================
 * SFTP/SCP 辅助函数 - 供 ts_sftp.c 和 ts_scp.c 调用
 * ============================================================================ */

/**
 * @brief 获取内部 libssh2 会话句柄
 */
LIBSSH2_SESSION *ts_ssh_get_libssh2_session(ts_ssh_session_t session)
{
    return session ? session->session : NULL;
}

/**
 * @brief 获取 socket 文件描述符
 */
int ts_ssh_get_socket(ts_ssh_session_t session)
{
    return session ? session->sock : -1;
}

/**
 * @brief 获取远程主机地址
 */
const char *ts_ssh_get_host(ts_ssh_session_t session)
{
    return session ? session->host_copy : NULL;
}

/**
 * @brief 获取远程端口
 */
uint16_t ts_ssh_get_port(ts_ssh_session_t session)
{
    return session ? session->config.port : 0;
}