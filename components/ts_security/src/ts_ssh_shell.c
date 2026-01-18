/**
 * @file ts_ssh_shell.c
 * @brief SSH Interactive Shell implementation using libssh2
 *
 * 实现交互式 Shell 功能：
 * - PTY（伪终端）分配
 * - 远程 Shell 启动
 * - 双向数据传输
 * - 终端窗口大小调整
 */

#include "ts_ssh_shell.h"
#include "ts_ssh_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <libssh2.h>

#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>

static const char *TAG = "ts_shell";

/* ============================================================================
 * 内部数据结构
 * ============================================================================ */

/** Shell 内部结构 */
struct ts_ssh_shell_s {
    ts_ssh_session_t ssh_session;       /**< SSH 会话 */
    LIBSSH2_CHANNEL *channel;           /**< SSH 通道 */
    ts_shell_config_t config;           /**< Shell 配置 */
    ts_shell_state_t state;             /**< 当前状态 */
    int exit_code;                      /**< 退出码 */
    
    /* 回调 */
    ts_shell_output_cb_t output_cb;
    void *output_cb_data;
    ts_shell_close_cb_t close_cb;
    void *close_cb_data;
    
    /* 读取缓冲区 */
    char read_buffer[1024];
};

/* 外部声明（来自 ts_ssh_client.c） */
extern LIBSSH2_SESSION *ts_ssh_get_libssh2_session(ts_ssh_session_t session);
extern int ts_ssh_get_socket(ts_ssh_session_t session);

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 获取终端类型字符串
 */
static const char *get_term_string(ts_term_type_t type)
{
    switch (type) {
        case TS_TERM_XTERM:  return "xterm";
        case TS_TERM_VT100:  return "vt100";
        case TS_TERM_VT220:  return "vt220";
        case TS_TERM_ANSI:   return "ansi";
        case TS_TERM_DUMB:   return "dumb";
        default:             return "xterm";
    }
}

/**
 * @brief 等待 socket 就绪
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

    dir = libssh2_session_block_directions(session);

    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) {
        readfd = &fd;
    }
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
        writefd = &fd;
    }

    return select(sock + 1, readfd, writefd, NULL, &timeout);
}

/* ============================================================================
 * 公开 API 实现
 * ============================================================================ */

esp_err_t ts_ssh_shell_open(ts_ssh_session_t session,
                             const ts_shell_config_t *config,
                             ts_ssh_shell_t *shell_out)
{
    if (!session || !shell_out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ts_ssh_is_connected(session)) {
        ESP_LOGE(TAG, "SSH session not connected");
        return ESP_ERR_INVALID_STATE;
    }

    LIBSSH2_SESSION *ssh = ts_ssh_get_libssh2_session(session);
    int sock = ts_ssh_get_socket(session);

    /* 分配 Shell 结构 */
    ts_ssh_shell_t shell = calloc(1, sizeof(struct ts_ssh_shell_s));
    if (!shell) {
        return ESP_ERR_NO_MEM;
    }

    shell->ssh_session = session;
    shell->state = TS_SHELL_STATE_IDLE;
    shell->exit_code = -1;

    /* 使用默认配置或用户配置 */
    if (config) {
        shell->config = *config;
    } else {
        ts_shell_config_t default_config = TS_SHELL_DEFAULT_CONFIG();
        shell->config = default_config;
    }

    /* 设置默认值 */
    if (shell->config.term_width == 0) {
        shell->config.term_width = 80;
    }
    if (shell->config.term_height == 0) {
        shell->config.term_height = 24;
    }
    if (shell->config.read_timeout_ms == 0) {
        shell->config.read_timeout_ms = 100;
    }

    /* 打开通道 */
    LIBSSH2_CHANNEL *channel = NULL;
    int rc;

    while ((channel = libssh2_channel_open_session(ssh)) == NULL &&
           libssh2_session_last_errno(ssh) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 1000);
    }

    if (!channel) {
        char *err_msg = NULL;
        libssh2_session_last_error(ssh, &err_msg, NULL, 0);
        ESP_LOGE(TAG, "Failed to open channel: %s", err_msg ? err_msg : "unknown");
        free(shell);
        return ESP_FAIL;
    }

    /* 请求 PTY */
    const char *term = get_term_string(shell->config.term_type);
    
    while ((rc = libssh2_channel_request_pty_ex(channel, 
                                                 term, strlen(term),
                                                 NULL, 0,  /* 终端模式（使用默认） */
                                                 shell->config.term_width,
                                                 shell->config.term_height,
                                                 0, 0)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 1000);
    }

    if (rc != 0) {
        char *err_msg = NULL;
        libssh2_session_last_error(ssh, &err_msg, NULL, 0);
        ESP_LOGE(TAG, "Failed to request PTY: %s", err_msg ? err_msg : "unknown");
        libssh2_channel_free(channel);
        free(shell);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "PTY allocated: %s %dx%d", term, 
             shell->config.term_width, shell->config.term_height);

    /* 启动 Shell */
    while ((rc = libssh2_channel_shell(channel)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 1000);
    }

    if (rc != 0) {
        char *err_msg = NULL;
        libssh2_session_last_error(ssh, &err_msg, NULL, 0);
        ESP_LOGE(TAG, "Failed to start shell: %s", err_msg ? err_msg : "unknown");
        libssh2_channel_free(channel);
        free(shell);
        return ESP_FAIL;
    }

    shell->channel = channel;
    shell->state = TS_SHELL_STATE_RUNNING;

    *shell_out = shell;
    ESP_LOGI(TAG, "Interactive shell opened");
    return ESP_OK;
}

esp_err_t ts_ssh_shell_close(ts_ssh_shell_t shell)
{
    if (!shell) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        return ESP_OK;
    }

    LIBSSH2_SESSION *ssh = ts_ssh_get_libssh2_session(shell->ssh_session);
    int sock = ts_ssh_get_socket(shell->ssh_session);

    /* 发送 EOF */
    int rc;
    while ((rc = libssh2_channel_send_eof(shell->channel)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 100);
    }

    /* 等待远程关闭 */
    while ((rc = libssh2_channel_wait_eof(shell->channel)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 100);
    }

    /* 关闭通道 */
    while ((rc = libssh2_channel_close(shell->channel)) == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 100);
    }

    /* 获取退出码 */
    if (rc == 0) {
        shell->exit_code = libssh2_channel_get_exit_status(shell->channel);
    }

    /* 调用关闭回调 */
    if (shell->close_cb) {
        shell->close_cb(shell->exit_code, shell->close_cb_data);
    }

    libssh2_channel_free(shell->channel);
    shell->channel = NULL;
    shell->state = TS_SHELL_STATE_CLOSED;

    ESP_LOGI(TAG, "Shell closed with exit code: %d", shell->exit_code);
    
    /* 释放结构 */
    free(shell);
    return ESP_OK;
}

esp_err_t ts_ssh_shell_write(ts_ssh_shell_t shell,
                              const char *data, size_t len,
                              size_t *written)
{
    if (!shell || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    LIBSSH2_SESSION *ssh = ts_ssh_get_libssh2_session(shell->ssh_session);
    int sock = ts_ssh_get_socket(shell->ssh_session);

    size_t total_written = 0;
    while (total_written < len) {
        int rc = libssh2_channel_write(shell->channel, data + total_written, 
                                        len - total_written);
        if (rc > 0) {
            total_written += rc;
        } else if (rc == LIBSSH2_ERROR_EAGAIN) {
            wait_socket(sock, ssh, 100);
        } else {
            ESP_LOGE(TAG, "Write error: %d", rc);
            return ESP_FAIL;
        }
    }

    if (written) {
        *written = total_written;
    }

    return ESP_OK;
}

esp_err_t ts_ssh_shell_read(ts_ssh_shell_t shell,
                             char *buffer, size_t buffer_size,
                             size_t *read_len)
{
    if (!shell || !buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        if (read_len) *read_len = 0;
        return ESP_ERR_INVALID_STATE;
    }

    int rc = libssh2_channel_read(shell->channel, buffer, buffer_size);
    
    if (rc > 0) {
        if (read_len) *read_len = rc;
        return ESP_OK;
    } else if (rc == 0) {
        /* EOF */
        if (read_len) *read_len = 0;
        shell->state = TS_SHELL_STATE_CLOSED;
        shell->exit_code = libssh2_channel_get_exit_status(shell->channel);
        return ESP_ERR_INVALID_STATE;
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        if (read_len) *read_len = 0;
        return ESP_ERR_TIMEOUT;
    } else {
        if (read_len) *read_len = 0;
        return ESP_FAIL;
    }
}

esp_err_t ts_ssh_shell_set_output_cb(ts_ssh_shell_t shell,
                                      ts_shell_output_cb_t cb,
                                      void *user_data)
{
    if (!shell) {
        return ESP_ERR_INVALID_ARG;
    }
    shell->output_cb = cb;
    shell->output_cb_data = user_data;
    return ESP_OK;
}

esp_err_t ts_ssh_shell_set_close_cb(ts_ssh_shell_t shell,
                                     ts_shell_close_cb_t cb,
                                     void *user_data)
{
    if (!shell) {
        return ESP_ERR_INVALID_ARG;
    }
    shell->close_cb = cb;
    shell->close_cb_data = user_data;
    return ESP_OK;
}

esp_err_t ts_ssh_shell_poll(ts_ssh_shell_t shell)
{
    if (!shell) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 检查通道是否关闭 */
    if (libssh2_channel_eof(shell->channel)) {
        shell->state = TS_SHELL_STATE_CLOSED;
        shell->exit_code = libssh2_channel_get_exit_status(shell->channel);
        if (shell->close_cb) {
            shell->close_cb(shell->exit_code, shell->close_cb_data);
        }
        return ESP_ERR_INVALID_STATE;
    }

    /* 读取可用数据 */
    int rc = libssh2_channel_read(shell->channel, shell->read_buffer, 
                                   sizeof(shell->read_buffer));
    
    if (rc > 0) {
        if (shell->output_cb) {
            shell->output_cb(shell->read_buffer, rc, shell->output_cb_data);
        }
        return ESP_OK;
    } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        return ESP_ERR_TIMEOUT;
    } else if (rc == 0) {
        shell->state = TS_SHELL_STATE_CLOSED;
        shell->exit_code = libssh2_channel_get_exit_status(shell->channel);
        if (shell->close_cb) {
            shell->close_cb(shell->exit_code, shell->close_cb_data);
        }
        return ESP_ERR_INVALID_STATE;
    } else {
        return ESP_FAIL;
    }
}

esp_err_t ts_ssh_shell_run(ts_ssh_shell_t shell,
                            ts_shell_output_cb_t output_cb,
                            ts_shell_input_cb_t input_cb,
                            void *user_data)
{
    if (!shell || !output_cb || !input_cb) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    LIBSSH2_SESSION *ssh = ts_ssh_get_libssh2_session(shell->ssh_session);
    int sock = ts_ssh_get_socket(shell->ssh_session);

    while (shell->state == TS_SHELL_STATE_RUNNING) {
        bool activity = false;
        
        /* 检查本地输入（优先处理，提高响应性） */
        size_t input_len = 0;
        const char *input = input_cb(&input_len, user_data);
        if (input && input_len > 0) {
            activity = true;
            size_t written = 0;
            while (written < input_len) {
                int rc = libssh2_channel_write(shell->channel, input + written,
                                            input_len - written);
                if (rc > 0) {
                    written += rc;
                } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                    wait_socket(sock, ssh, 10);
                } else {
                    shell->state = TS_SHELL_STATE_ERROR;
                    return ESP_FAIL;
                }
            }
        }

        /* 持续读取远程数据直到没有更多数据 */
        int rc;
        do {
            rc = libssh2_channel_read(shell->channel, shell->read_buffer,
                                       sizeof(shell->read_buffer));
            if (rc > 0) {
                activity = true;
                output_cb(shell->read_buffer, rc, user_data);
            } else if (rc == 0 || libssh2_channel_eof(shell->channel)) {
                /* 远程关闭 */
                shell->exit_code = libssh2_channel_get_exit_status(shell->channel);
                shell->state = TS_SHELL_STATE_CLOSED;
                return ESP_OK;
            }
        } while (rc > 0);  /* 继续读取直到 EAGAIN */

        if (rc < 0 && rc != LIBSSH2_ERROR_EAGAIN) {
            /* 真正的错误 */
            shell->state = TS_SHELL_STATE_ERROR;
            return ESP_FAIL;
        }

        /* 只在没有活动时短暂等待，避免 CPU 空转 */
        if (!activity) {
            wait_socket(sock, ssh, 10);  /* 减少等待时间到 10ms */
        }
    }

    shell->exit_code = libssh2_channel_get_exit_status(shell->channel);
    shell->state = TS_SHELL_STATE_CLOSED;
    
    return ESP_OK;
}

esp_err_t ts_ssh_shell_resize(ts_ssh_shell_t shell,
                               uint16_t width, uint16_t height)
{
    if (!shell) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    LIBSSH2_SESSION *ssh = ts_ssh_get_libssh2_session(shell->ssh_session);
    int sock = ts_ssh_get_socket(shell->ssh_session);

    int rc;
    while ((rc = libssh2_channel_request_pty_size(shell->channel, width, height)) 
           == LIBSSH2_ERROR_EAGAIN) {
        wait_socket(sock, ssh, 100);
    }

    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to resize PTY: %d", rc);
        return ESP_FAIL;
    }

    shell->config.term_width = width;
    shell->config.term_height = height;
    ESP_LOGD(TAG, "Terminal resized to %dx%d", width, height);
    
    return ESP_OK;
}

esp_err_t ts_ssh_shell_send_signal(ts_ssh_shell_t shell, const char *signal_name)
{
    if (!shell || !signal_name) {
        return ESP_ERR_INVALID_ARG;
    }

    if (shell->state != TS_SHELL_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 对于常用信号，直接发送对应的控制字符 */
    if (strcmp(signal_name, "INT") == 0) {
        /* Ctrl+C */
        return ts_ssh_shell_write(shell, "\x03", 1, NULL);
    } else if (strcmp(signal_name, "QUIT") == 0) {
        /* Ctrl+\ */
        return ts_ssh_shell_write(shell, "\x1c", 1, NULL);
    } else if (strcmp(signal_name, "TSTP") == 0) {
        /* Ctrl+Z */
        return ts_ssh_shell_write(shell, "\x1a", 1, NULL);
    } else if (strcmp(signal_name, "EOF") == 0) {
        /* Ctrl+D */
        return ts_ssh_shell_write(shell, "\x04", 1, NULL);
    }

    /* 其他信号：尝试使用 libssh2 的信号功能 */
    /* 注意：并非所有服务器都支持 */
    ESP_LOGW(TAG, "Signal %s may not be supported", signal_name);
    return ESP_ERR_NOT_SUPPORTED;
}

ts_shell_state_t ts_ssh_shell_get_state(ts_ssh_shell_t shell)
{
    return shell ? shell->state : TS_SHELL_STATE_IDLE;
}

bool ts_ssh_shell_is_active(ts_ssh_shell_t shell)
{
    return shell && shell->state == TS_SHELL_STATE_RUNNING;
}

int ts_ssh_shell_get_exit_code(ts_ssh_shell_t shell)
{
    return shell ? shell->exit_code : -1;
}
