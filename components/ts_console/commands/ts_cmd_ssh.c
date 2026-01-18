/**
 * @file ts_cmd_ssh.c
 * @brief SSH Console Commands
 * 
 * 实现 ssh 命令：
 * - ssh --host <ip> --user <user> --password <pwd> --exec <cmd>
 * - ssh --test --host <ip> --user <user> --password <pwd>
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-18
 */

#include "ts_console.h"
#include "ts_log.h"
#include "ts_ssh_client.h"
#include "ts_ssh_shell.h"
#include "ts_port_forward.h"
#include "argtable3/argtable3.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_ssh"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_str *host;       /**< SSH 服务器地址 */
    struct arg_int *port;       /**< SSH 端口 (默认 22) */
    struct arg_str *user;       /**< 用户名 */
    struct arg_str *password;   /**< 密码 */
    struct arg_str *exec;       /**< 要执行的命令 */
    struct arg_lit *test;       /**< 测试连接 */
    struct arg_lit *shell;      /**< 交互式 Shell */
    struct arg_str *forward;    /**< 端口转发 L<local>:<remote_host>:<remote_port> */
    struct arg_int *timeout;    /**< 超时时间（秒） */
    struct arg_lit *verbose;    /**< 详细输出 */
    struct arg_lit *help;
    struct arg_end *end;
} s_ssh_args;

/*===========================================================================*/
/*                          流式输出回调                                      */
/*===========================================================================*/

/** 用于传递给回调的上下文 */
typedef struct {
    ts_ssh_session_t session;
    bool verbose;
} stream_context_t;

static void stream_output_callback(const char *data, size_t len, bool is_stderr, void *user_data)
{
    stream_context_t *ctx = (stream_context_t *)user_data;
    
    /* 检查中断请求 */
    if (ts_console_interrupted()) {
        ts_ssh_abort(ctx->session);
        return;
    }
    
    /* 打印输出 */
    if (is_stderr && ctx->verbose) {
        ts_console_printf("\033[31m");  /* 红色表示 stderr */
    }
    
    /* 直接输出数据 */
    for (size_t i = 0; i < len; i++) {
        ts_console_printf("%c", data[i]);
    }
    
    if (is_stderr && ctx->verbose) {
        ts_console_printf("\033[0m");   /* 重置颜色 */
    }
}

/*===========================================================================*/
/*                          Ctrl+C 检测任务                                   */
/*===========================================================================*/

typedef struct {
    ts_ssh_session_t session;
    volatile bool running;
} interrupt_monitor_t;

static void interrupt_monitor_task(void *arg)
{
    interrupt_monitor_t *mon = (interrupt_monitor_t *)arg;
    
    while (mon->running) {
        /* 非阻塞检查 UART 输入 */
        uint8_t ch;
        int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            if (ch == 0x03) {  /* Ctrl+C = ASCII 0x03 */
                ts_console_printf("\n^C\n");
                ts_console_request_interrupt();
                ts_ssh_abort(mon->session);
                break;
            } else if (ch == 0x1C) {  /* Ctrl+\ = ASCII 0x1C (SIGQUIT) */
                ts_console_printf("\n^\\\n");
                ts_console_request_interrupt();
                ts_ssh_abort(mon->session);
                break;
            }
        }
    }
    
    vTaskDelete(NULL);
}

/*===========================================================================*/
/*                          Command: ssh --exec                               */
/*===========================================================================*/

static int do_ssh_exec(const char *host, int port, const char *user, 
                       const char *password, const char *command, 
                       int timeout_sec, bool verbose)
{
    ts_ssh_session_t session = NULL;
    esp_err_t ret;
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = password;
    config.timeout_ms = timeout_sec * 1000;
    
    if (verbose) {
        ts_console_printf("Connecting to %s@%s:%d...\n", user, host, port);
    }
    
    /* 创建会话 */
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create SSH session\n");
        return 1;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    if (verbose) {
        ts_console_printf("Connected. Executing command: %s\n", command);
        ts_console_printf("(Press Ctrl+C to abort)\n\n");
    }
    
    /* 启动中断监测任务 */
    interrupt_monitor_t monitor = {
        .session = session,
        .running = true
    };
    TaskHandle_t monitor_task = NULL;
    ts_console_clear_interrupt();
    
    xTaskCreate(interrupt_monitor_task, "ssh_mon", 3072, &monitor, 5, &monitor_task);
    
    /* 执行命令（流式输出） */
    int exit_code = -1;
    stream_context_t ctx = {
        .session = session,
        .verbose = verbose
    };
    
    ret = ts_ssh_exec_stream(session, command, stream_output_callback, &ctx, &exit_code);
    
    /* 停止监测任务 */
    monitor.running = false;
    vTaskDelay(pdMS_TO_TICKS(100));  /* 等待任务退出 */
    
    if (ret == ESP_ERR_TIMEOUT) {
        ts_console_printf("\n--- Command aborted by user ---\n");
    } else if (ret != ESP_OK) {
        ts_console_printf("\nError: Failed to execute command - %s\n", 
                          ts_ssh_get_error(session));
    } else if (verbose) {
        ts_console_printf("\n--- Command completed with exit code: %d ---\n", exit_code);
    }
    
    /* 断开连接 */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    ts_console_clear_interrupt();
    
    return (ret == ESP_OK) ? exit_code : 1;
}

/*===========================================================================*/
/*                          Command: ssh --shell                              */
/*===========================================================================*/

/** Shell 输出回调 */
static void shell_output_callback(const char *data, size_t len, void *user_data)
{
    (void)user_data;
    /* 直接输出到 stdout，更高效 */
    fwrite(data, 1, len, stdout);
    fflush(stdout);  /* 立即刷新，确保显示 */
}

/** Shell 输入回调 */
static const char *shell_input_callback(size_t *out_len, void *user_data)
{
    static char input_buf[64];
    ts_ssh_shell_t shell = (ts_ssh_shell_t)user_data;
    
    *out_len = 0;
    
    /* 非阻塞读取 UART（0ms 超时 = 立即返回） */
    uint8_t ch;
    int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, 0);
    if (len > 0) {
        /* 检查特殊控制字符 */
        if (ch == 0x03) {  /* Ctrl+C */
            /* 发送 SIGINT 到远程 */
            ts_ssh_shell_send_signal(shell, "INT");
            return NULL;
        } else if (ch == 0x1C) {  /* Ctrl+\ - 退出 Shell */
            ts_console_printf("\n^\\  (Exit shell)\n");
            ts_console_request_interrupt();
            return NULL;
        }
        
        input_buf[0] = ch;
        *out_len = 1;
        return input_buf;
    }
    
    return NULL;
}

static int do_ssh_shell(const char *host, int port, const char *user,
                        const char *password, int timeout_sec, bool verbose)
{
    ts_ssh_session_t session = NULL;
    ts_ssh_shell_t shell = NULL;
    esp_err_t ret;
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = password;
    config.timeout_ms = timeout_sec * 1000;
    
    ts_console_printf("Connecting to %s@%s:%d...\n", user, host, port);
    
    /* 创建会话 */
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create SSH session\n");
        return 1;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    /* 打开交互式 Shell */
    ts_shell_config_t shell_config = TS_SHELL_DEFAULT_CONFIG();
    shell_config.term_width = 80;
    shell_config.term_height = 24;
    shell_config.read_timeout_ms = 50;
    
    ret = ts_ssh_shell_open(session, &shell_config, &shell);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to open shell\n");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    ts_console_printf("Connected. Interactive shell started.\n");
    ts_console_printf("(Press Ctrl+\\ to exit, Ctrl+C to send SIGINT)\n\n");
    ts_console_clear_interrupt();
    
    /* 运行交互式 Shell 循环 */
    ret = ts_ssh_shell_run(shell, shell_output_callback, shell_input_callback, shell);
    
    int exit_code = ts_ssh_shell_get_exit_code(shell);
    
    /* 清理 */
    ts_ssh_shell_close(shell);
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    ts_console_clear_interrupt();
    
    ts_console_printf("\n--- Shell closed (exit code: %d) ---\n", exit_code);
    
    return (ret == ESP_OK) ? 0 : 1;
}

/*===========================================================================*/
/*                          Command: ssh --forward                            */
/*===========================================================================*/

static int do_ssh_forward(const char *host, int port, const char *user,
                          const char *password, const char *forward_spec,
                          int timeout_sec, bool verbose)
{
    ts_ssh_session_t session = NULL;
    ts_port_forward_t forward = NULL;
    esp_err_t ret;
    
    /* 解析端口转发规格: L<local_port>:<remote_host>:<remote_port> */
    if (forward_spec[0] != 'L' && forward_spec[0] != 'l') {
        ts_console_printf("Error: Forward spec must start with 'L' (local forward)\n");
        ts_console_printf("Format: L<local_port>:<remote_host>:<remote_port>\n");
        ts_console_printf("Example: L8080:localhost:80\n");
        return 1;
    }
    
    int local_port = 0;
    char remote_host[128] = {0};
    int remote_port = 0;
    
    if (sscanf(forward_spec + 1, "%d:%127[^:]:%d", &local_port, remote_host, &remote_port) != 3) {
        ts_console_printf("Error: Invalid forward spec format\n");
        ts_console_printf("Format: L<local_port>:<remote_host>:<remote_port>\n");
        ts_console_printf("Example: L8080:localhost:80\n");
        return 1;
    }
    
    if (local_port <= 0 || local_port > 65535 || remote_port <= 0 || remote_port > 65535) {
        ts_console_printf("Error: Invalid port number\n");
        return 1;
    }
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = password;
    config.timeout_ms = timeout_sec * 1000;
    
    ts_console_printf("Connecting to %s@%s:%d...\n", user, host, port);
    
    /* 创建会话 */
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create SSH session\n");
        return 1;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    /* 创建端口转发 */
    ts_forward_config_t fwd_config = TS_FORWARD_DEFAULT_CONFIG();
    fwd_config.direction = TS_FORWARD_LOCAL;
    fwd_config.local_host = "0.0.0.0";  /* 允许所有接口访问 */
    fwd_config.local_port = local_port;
    fwd_config.remote_host = remote_host;
    fwd_config.remote_port = remote_port;
    
    ret = ts_port_forward_create(session, &fwd_config, &forward);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create port forward\n");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    /* 启动转发 */
    ret = ts_port_forward_start(forward);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to start port forward\n");
        ts_port_forward_destroy(forward);
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    ts_console_printf("\n");
    ts_console_printf("╔════════════════════════════════════════════╗\n");
    ts_console_printf("║         SSH Port Forwarding Active         ║\n");
    ts_console_printf("╠════════════════════════════════════════════╣\n");
    ts_console_printf("║  Local:   0.0.0.0:%-5d                    ║\n", local_port);
    ts_console_printf("║  Remote:  %s:%-5d", remote_host, remote_port);
    /* 填充空格 */
    int len = strlen(remote_host) + 6;
    for (int i = len; i < 25; i++) ts_console_printf(" ");
    ts_console_printf("║\n");
    ts_console_printf("╠════════════════════════════════════════════╣\n");
    ts_console_printf("║  Press Ctrl+C to stop forwarding           ║\n");
    ts_console_printf("╚════════════════════════════════════════════╝\n\n");
    
    ts_console_clear_interrupt();
    
    /* 等待用户中断 */
    while (!ts_console_interrupted()) {
        /* 检查 UART 输入 */
        uint8_t ch;
        int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, pdMS_TO_TICKS(500));
        if (len > 0 && (ch == 0x03 || ch == 0x1C)) {
            ts_console_printf("\n^C\n");
            break;
        }
        
        /* 显示统计（每 5 秒） */
        if (verbose) {
            static int counter = 0;
            if (++counter >= 10) {
                counter = 0;
                ts_forward_stats_t stats;
                if (ts_port_forward_get_stats(forward, &stats) == ESP_OK) {
                    ts_console_printf("Stats: %u active, %u total, TX: %llu, RX: %llu\r",
                                      stats.active_connections, stats.total_connections,
                                      stats.bytes_sent, stats.bytes_received);
                }
            }
        }
    }
    
    /* 停止转发 */
    ts_console_printf("Stopping port forward...\n");
    ts_port_forward_stop(forward);
    ts_port_forward_destroy(forward);
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    ts_console_clear_interrupt();
    
    ts_console_printf("Port forwarding stopped.\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: ssh --test                               */
/*===========================================================================*/

static int do_ssh_test(const char *host, int port, const char *user, 
                       const char *password, int timeout_sec)
{
    ts_ssh_session_t session = NULL;
    esp_err_t ret;
    
    ts_console_printf("\nSSH Connection Test\n");
    ts_console_printf("═══════════════════════════════════════\n");
    ts_console_printf("  Host:     %s\n", host);
    ts_console_printf("  Port:     %d\n", port);
    ts_console_printf("  User:     %s\n", user);
    ts_console_printf("  Timeout:  %d seconds\n", timeout_sec);
    ts_console_printf("═══════════════════════════════════════\n\n");
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = password;
    config.timeout_ms = timeout_sec * 1000;
    
    ts_console_printf("[1/3] Creating session... ");
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        return 1;
    }
    ts_console_printf("OK\n");
    
    ts_console_printf("[2/3] Connecting and authenticating... ");
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    ts_console_printf("OK\n");
    
    ts_console_printf("[3/3] Testing command execution... ");
    ts_ssh_exec_result_t result;
    ret = ts_ssh_exec(session, "echo 'TianShanOS SSH test'", &result);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    bool test_ok = (result.exit_code == 0 && 
                    result.stdout_data && 
                    strstr(result.stdout_data, "TianShanOS") != NULL);
    ts_ssh_exec_result_free(&result);
    
    if (test_ok) {
        ts_console_printf("OK\n");
    } else {
        ts_console_printf("FAILED\n");
    }
    
    /* 断开连接 */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    ts_console_printf("\n");
    if (test_ok) {
        ts_console_printf("✓ SSH connection test PASSED\n");
        return 0;
    } else {
        ts_console_printf("✗ SSH connection test FAILED\n");
        return 1;
    }
}

/*===========================================================================*/
/*                          Command Handler                                   */
/*===========================================================================*/

static int ssh_cmd_handler(int argc, char **argv)
{
    /* 解析参数 */
    int nerrors = arg_parse(argc, argv, (void **)&s_ssh_args);
    
    /* 显示帮助 */
    if (s_ssh_args.help->count > 0) {
        ts_console_printf("\nUsage: ssh [options]\n\n");
        ts_console_printf("SSH client for remote operations\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  --host <ip>       Remote host address (required)\n");
        ts_console_printf("  --port <num>      SSH port (default: 22)\n");
        ts_console_printf("  --user <name>     Username (required)\n");
        ts_console_printf("  --password <pwd>  Password (required)\n");
        ts_console_printf("  --exec <cmd>      Execute command on remote host\n");
        ts_console_printf("  --shell           Open interactive shell\n");
        ts_console_printf("  --forward <spec>  Port forwarding: L<local>:<remote_host>:<remote_port>\n");
        ts_console_printf("  --test            Test SSH connection\n");
        ts_console_printf("  --timeout <sec>   Connection timeout in seconds (default: 10)\n");
        ts_console_printf("  --verbose         Show detailed output\n");
        ts_console_printf("  --help            Show this help\n");
        ts_console_printf("\nExamples:\n");
        ts_console_printf("  ssh --test --host 192.168.1.100 --user root --password secret\n");
        ts_console_printf("  ssh --host 192.168.1.100 --user nvidia --password nvidia --exec \"uptime\"\n");
        ts_console_printf("  ssh --host agx.local --user root --password root --shell\n");
        ts_console_printf("  ssh --host agx --user root --password root --forward L8080:localhost:80\n");
        return 0;
    }
    
    /* 参数错误检查 */
    if (nerrors > 0) {
        arg_print_errors(stderr, s_ssh_args.end, "ssh");
        ts_console_printf("Use 'ssh --help' for usage information\n");
        return 1;
    }
    
    /* 必需参数检查 */
    if (s_ssh_args.host->count == 0) {
        ts_console_printf("Error: --host is required\n");
        return 1;
    }
    
    if (s_ssh_args.user->count == 0) {
        ts_console_printf("Error: --user is required\n");
        return 1;
    }
    
    if (s_ssh_args.password->count == 0) {
        ts_console_printf("Error: --password is required\n");
        return 1;
    }
    
    /* 获取参数值 */
    const char *host = s_ssh_args.host->sval[0];
    int port = (s_ssh_args.port->count > 0) ? s_ssh_args.port->ival[0] : 22;
    const char *user = s_ssh_args.user->sval[0];
    const char *password = s_ssh_args.password->sval[0];
    int timeout = (s_ssh_args.timeout->count > 0) ? s_ssh_args.timeout->ival[0] : 10;
    bool verbose = (s_ssh_args.verbose->count > 0);
    
    /* 执行操作（按优先级） */
    if (s_ssh_args.shell->count > 0) {
        /* 交互式 Shell */
        return do_ssh_shell(host, port, user, password, timeout, verbose);
    } else if (s_ssh_args.forward->count > 0) {
        /* 端口转发 */
        return do_ssh_forward(host, port, user, password, 
                              s_ssh_args.forward->sval[0], timeout, verbose);
    } else if (s_ssh_args.exec->count > 0) {
        /* 执行命令 */
        const char *command = s_ssh_args.exec->sval[0];
        return do_ssh_exec(host, port, user, password, command, timeout, verbose);
    } else if (s_ssh_args.test->count > 0) {
        /* 测试连接 */
        return do_ssh_test(host, port, user, password, timeout);
    } else {
        ts_console_printf("Error: Specify --exec, --shell, --forward, or --test\n");
        ts_console_printf("Use 'ssh --help' for usage information\n");
        return 1;
    }
}

/*===========================================================================*/
/*                          Command Registration                              */
/*===========================================================================*/

esp_err_t ts_cmd_ssh_register(void)
{
    /* 初始化参数表 */
    s_ssh_args.host = arg_str0(NULL, "host", "<ip>", "Remote host address");
    s_ssh_args.port = arg_int0(NULL, "port", "<num>", "SSH port (default: 22)");
    s_ssh_args.user = arg_str0(NULL, "user", "<name>", "Username");
    s_ssh_args.password = arg_str0(NULL, "password", "<pwd>", "Password");
    s_ssh_args.exec = arg_str0(NULL, "exec", "<cmd>", "Command to execute");
    s_ssh_args.test = arg_lit0(NULL, "test", "Test SSH connection");
    s_ssh_args.shell = arg_lit0(NULL, "shell", "Open interactive shell");
    s_ssh_args.forward = arg_str0(NULL, "forward", "<spec>", "Port forward: L<local>:<host>:<port>");
    s_ssh_args.timeout = arg_int0(NULL, "timeout", "<sec>", "Timeout in seconds");
    s_ssh_args.verbose = arg_lit0("v", "verbose", "Verbose output");
    s_ssh_args.help = arg_lit0("h", "help", "Show help");
    s_ssh_args.end = arg_end(5);
    
    /* 注册命令 */
    const esp_console_cmd_t cmd = {
        .command = "ssh",
        .help = "SSH client for remote command execution. Use 'ssh --help' for details.",
        .hint = NULL,
        .func = ssh_cmd_handler,
        .argtable = &s_ssh_args,
    };
    
    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register ssh command: %s", esp_err_to_name(ret));
    } else {
        TS_LOGI(TAG, "Registered command: ssh");
    }
    
    return ret;
}
