/**
 * @file ts_i18n.c
 * @brief TianShanOS Internationalization Implementation
 */

#include "ts_i18n.h"
#include "ts_log.h"
#include "ts_config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TAG "ts_i18n"

/*===========================================================================*/
/*                              String Tables                                 */
/*===========================================================================*/

/* English strings (default) */
static const char *s_strings_en[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "Welcome to TianShanOS",
    [TS_STR_VERSION]           = "Version",
    [TS_STR_READY]             = "Ready",
    [TS_STR_ERROR]             = "Error",
    [TS_STR_SUCCESS]           = "Success",
    [TS_STR_FAILED]            = "Failed",
    [TS_STR_UNKNOWN_CMD]       = "Unknown command",
    [TS_STR_HELP_HEADER]       = "Available commands:",
    [TS_STR_USAGE]             = "Usage",
    
    /* Common prompts */
    [TS_STR_YES]               = "Yes",
    [TS_STR_NO]                = "No",
    [TS_STR_OK]                = "OK",
    [TS_STR_CANCEL]            = "Cancel",
    [TS_STR_CONFIRM]           = "Confirm",
    [TS_STR_LOADING]           = "Loading...",
    [TS_STR_PLEASE_WAIT]       = "Please wait...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "Device Information",
    [TS_STR_UPTIME]            = "Uptime",
    [TS_STR_FREE_HEAP]         = "Free Heap",
    [TS_STR_CHIP_MODEL]        = "Chip Model",
    [TS_STR_FIRMWARE_VER]      = "Firmware Version",
    [TS_STR_TEMPERATURE]       = "Temperature",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi connected",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi disconnected",
    [TS_STR_WIFI_SCANNING]     = "Scanning WiFi networks...",
    [TS_STR_WIFI_CONNECTING]   = "Connecting to WiFi...",
    [TS_STR_IP_ADDRESS]        = "IP Address",
    [TS_STR_MAC_ADDRESS]       = "MAC Address",
    [TS_STR_SIGNAL_STRENGTH]   = "Signal Strength",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LED Controller",
    [TS_STR_LED_COUNT]         = "LED Count",
    [TS_STR_BRIGHTNESS]        = "Brightness",
    [TS_STR_EFFECT]            = "Effect",
    [TS_STR_COLOR]             = "Color",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "Voltage",
    [TS_STR_CURRENT]           = "Current",
    [TS_STR_POWER]             = "Power",
    [TS_STR_POWER_GOOD]        = "Power Good",
    [TS_STR_POWER_OFF]         = "Power Off",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "Invalid argument",
    [TS_STR_ERR_NOT_FOUND]     = "Not found",
    [TS_STR_ERR_NO_MEM]        = "Out of memory",
    [TS_STR_ERR_TIMEOUT]       = "Timeout",
    [TS_STR_ERR_NOT_SUPPORTED] = "Not supported",
    [TS_STR_ERR_INVALID_STATE] = "Invalid state",
    [TS_STR_ERR_IO]            = "I/O error",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "Rebooting...",
    [TS_STR_SHUTTING_DOWN]     = "Shutting down...",
    [TS_STR_REBOOT_IN]         = "Rebooting in %d seconds",
    
    /* Help messages */
    [TS_STR_HELP_USE_CMD]      = "Use '<command> --help' for command details",
    [TS_STR_SSH_HELP_USE]      = "Use 'ssh --help' for usage information",
    [TS_STR_SSH_HELP_USAGE]    = "Usage: ssh [options]",
    [TS_STR_SSH_HELP_DESC]     = "SSH client for remote operations",
    [TS_STR_SSH_HELP_CONN_OPTS]= "Connection Options:",
    [TS_STR_SSH_HELP_HOST]     = "  --host <ip>       Remote host address",
    [TS_STR_SSH_HELP_PORT]     = "  --port <num>      SSH port (default: 22)",
    [TS_STR_SSH_HELP_USER]    = "  --user <name>     Username",
    [TS_STR_SSH_HELP_PASSWORD] = "  --password <pwd>  Password (for password auth)",
    [TS_STR_SSH_HELP_KEY]      = "  --key <path>      Private key file (for public key auth)",
    [TS_STR_SSH_HELP_KEYID]   = "  --keyid <id>      Use key from secure storage (see 'key' command)",
    [TS_STR_SSH_HELP_EXEC]    = "  --exec <cmd>      Execute command on remote host",
    [TS_STR_SSH_HELP_SHELL]   = "  --shell           Open interactive shell",
    [TS_STR_SSH_HELP_FORWARD] = "  --forward <spec>  Port forwarding: L<local>:<remote_host>:<remote_port>",
    [TS_STR_SSH_HELP_TEST]    = "  --test            Test SSH connection",
    [TS_STR_SSH_HELP_TIMEOUT] = "  --timeout <sec>   Connection timeout in seconds (default: 10)",
    [TS_STR_SSH_HELP_VERBOSE] = "  --verbose         Show detailed output",
    [TS_STR_SSH_HELP_KEY_MGMT]= "Key File Management:",
    [TS_STR_SSH_HELP_KEYGEN]  = "  --keygen          Generate SSH key pair to file",
    [TS_STR_SSH_HELP_COPYID]  = "  --copyid          Deploy public key to remote server",
    [TS_STR_SSH_HELP_REVOKE]  = "  --revoke          Remove public key from remote server",
    [TS_STR_SSH_HELP_TYPE]    = "  --type <type>     Key type: rsa, rsa2048, rsa4096, ecdsa, ec256, ec384",
    [TS_STR_SSH_HELP_OUTPUT]  = "  --output <path>   Output file path for private key",
    [TS_STR_SSH_HELP_COMMENT]  = "  --comment <text>  Comment for the public key",
    [TS_STR_SSH_HELP_GENERAL] = "General:",
    [TS_STR_SSH_HELP_HELP_OPT]= "  --help            Show this help",
    [TS_STR_SSH_HELP_EXAMPLES]= "Examples:",
    [TS_STR_SSH_HELP_EX_KEYGEN]= "  # Generate RSA key pair to file",
    [TS_STR_SSH_HELP_EX_KEYGEN_CMD] = "  ssh --keygen --type rsa2048 --output /sdcard/id_rsa",
    [TS_STR_SSH_HELP_EX_STORED]= "  # Connect using stored key (manage keys with 'key' command)",
    [TS_STR_SSH_HELP_EX_KEY_LIST] = "  key --list                                          # List stored keys",
    [TS_STR_SSH_HELP_EX_KEY_GEN]  = "  key --generate --id agx --type rsa                  # Generate RSA key",
    [TS_STR_SSH_HELP_EX_SSH]  = "  ssh --host 192.168.1.100 --user nvidia --keyid agx --shell",
    [TS_STR_SSH_HELP_EX_COPYID]= "  # Deploy public key to remote server (using secure storage key)",
    [TS_STR_SSH_HELP_EX_COPYID_CMD] = "  ssh --copyid --host 192.168.1.100 --user nvidia --password pw --keyid agx",
    [TS_STR_SSH_HELP_EX_REVOKE]= "  # Revoke (remove) deployed public key from remote server",
    [TS_STR_SSH_HELP_EX_REVOKE_CMD] = "  ssh --revoke --host 192.168.1.100 --user nvidia --password pw --keyid agx",
    [TS_STR_SSH_HELP_EX_FILE] = "  # Or deploy using file-based key",
    [TS_STR_SSH_HELP_EX_FILE_CMD] = "  ssh --copyid --host 192.168.1.100 --user nvidia --password pw --key /sdcard/id_rsa",
    [TS_STR_SSH_HELP_NOTE]    = "Note: Key management has moved to the 'key' command. Use 'key --help' for details.",
    [TS_STR_SSH_HELP_NOTE_HOSTS] = "      Use 'hosts' command to manage known hosts.",
};

/* Simplified Chinese strings */
static const char *s_strings_zh_cn[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "欢迎使用天山操作系统",
    [TS_STR_VERSION]           = "版本",
    [TS_STR_READY]             = "就绪",
    [TS_STR_ERROR]             = "错误",
    [TS_STR_SUCCESS]           = "成功",
    [TS_STR_FAILED]            = "失败",
    [TS_STR_UNKNOWN_CMD]       = "未知命令",
    [TS_STR_HELP_HEADER]       = "可用命令:",
    [TS_STR_USAGE]             = "用法",
    
    /* Common prompts */
    [TS_STR_YES]               = "是",
    [TS_STR_NO]                = "否",
    [TS_STR_OK]                = "确定",
    [TS_STR_CANCEL]            = "取消",
    [TS_STR_CONFIRM]           = "确认",
    [TS_STR_LOADING]           = "加载中...",
    [TS_STR_PLEASE_WAIT]       = "请稍候...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "设备信息",
    [TS_STR_UPTIME]            = "运行时间",
    [TS_STR_FREE_HEAP]         = "可用内存",
    [TS_STR_CHIP_MODEL]        = "芯片型号",
    [TS_STR_FIRMWARE_VER]      = "固件版本",
    [TS_STR_TEMPERATURE]       = "温度",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi 已连接",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi 已断开",
    [TS_STR_WIFI_SCANNING]     = "正在扫描 WiFi 网络...",
    [TS_STR_WIFI_CONNECTING]   = "正在连接 WiFi...",
    [TS_STR_IP_ADDRESS]        = "IP 地址",
    [TS_STR_MAC_ADDRESS]       = "MAC 地址",
    [TS_STR_SIGNAL_STRENGTH]   = "信号强度",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LED 控制器",
    [TS_STR_LED_COUNT]         = "LED 数量",
    [TS_STR_BRIGHTNESS]        = "亮度",
    [TS_STR_EFFECT]            = "特效",
    [TS_STR_COLOR]             = "颜色",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "电压",
    [TS_STR_CURRENT]           = "电流",
    [TS_STR_POWER]             = "功率",
    [TS_STR_POWER_GOOD]        = "电源正常",
    [TS_STR_POWER_OFF]         = "电源关闭",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "无效参数",
    [TS_STR_ERR_NOT_FOUND]     = "未找到",
    [TS_STR_ERR_NO_MEM]        = "内存不足",
    [TS_STR_ERR_TIMEOUT]       = "超时",
    [TS_STR_ERR_NOT_SUPPORTED] = "不支持",
    [TS_STR_ERR_INVALID_STATE] = "状态无效",
    [TS_STR_ERR_IO]            = "I/O 错误",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "正在重启...",
    [TS_STR_SHUTTING_DOWN]     = "正在关机...",
    [TS_STR_REBOOT_IN]         = "%d 秒后重启",
    
    /* Help messages */
    [TS_STR_HELP_USE_CMD]      = "使用 '<命令> --help' 查看命令详情",
    [TS_STR_SSH_HELP_USE]      = "使用 'ssh --help' 查看用法",
    [TS_STR_SSH_HELP_USAGE]    = "用法: ssh [选项]",
    [TS_STR_SSH_HELP_DESC]     = "SSH 远程操作客户端",
    [TS_STR_SSH_HELP_CONN_OPTS]= "连接选项:",
    [TS_STR_SSH_HELP_HOST]     = "  --host <ip>       远程主机地址",
    [TS_STR_SSH_HELP_PORT]     = "  --port <num>     SSH 端口 (默认: 22)",
    [TS_STR_SSH_HELP_USER]    = "  --user <name>     用户名",
    [TS_STR_SSH_HELP_PASSWORD] = "  --password <pwd>  密码 (用于密码认证)",
    [TS_STR_SSH_HELP_KEY]      = "  --key <path>      私钥文件 (用于公钥认证)",
    [TS_STR_SSH_HELP_KEYID]   = "  --keyid <id>      使用安全存储中的密钥 (见 'key' 命令)",
    [TS_STR_SSH_HELP_EXEC]    = "  --exec <cmd>      在远程主机执行命令",
    [TS_STR_SSH_HELP_SHELL]   = "  --shell           打开交互式 shell",
    [TS_STR_SSH_HELP_FORWARD] = "  --forward <spec>  端口转发: L<本地>:<远程主机>:<远程端口>",
    [TS_STR_SSH_HELP_TEST]    = "  --test            测试 SSH 连接",
    [TS_STR_SSH_HELP_TIMEOUT] = "  --timeout <sec>   连接超时秒数 (默认: 10)",
    [TS_STR_SSH_HELP_VERBOSE] = "  --verbose         显示详细输出",
    [TS_STR_SSH_HELP_KEY_MGMT]= "密钥文件管理:",
    [TS_STR_SSH_HELP_KEYGEN]  = "  --keygen          生成 SSH 密钥对到文件",
    [TS_STR_SSH_HELP_COPYID]  = "  --copyid          部署公钥到远程服务器",
    [TS_STR_SSH_HELP_REVOKE]  = "  --revoke          从远程服务器撤销公钥",
    [TS_STR_SSH_HELP_TYPE]    = "  --type <type>     密钥类型: rsa, rsa2048, rsa4096, ecdsa, ec256, ec384",
    [TS_STR_SSH_HELP_OUTPUT]  = "  --output <path>   私钥输出文件路径",
    [TS_STR_SSH_HELP_COMMENT]  = "  --comment <text>  公钥注释",
    [TS_STR_SSH_HELP_GENERAL] = "通用:",
    [TS_STR_SSH_HELP_HELP_OPT]= "  --help            显示此帮助",
    [TS_STR_SSH_HELP_EXAMPLES]= "示例:",
    [TS_STR_SSH_HELP_EX_KEYGEN]= "  # 生成 RSA 密钥对到文件",
    [TS_STR_SSH_HELP_EX_KEYGEN_CMD] = "  ssh --keygen --type rsa2048 --output /sdcard/id_rsa",
    [TS_STR_SSH_HELP_EX_STORED]= "  # 使用存储的密钥连接 (使用 'key' 命令管理密钥)",
    [TS_STR_SSH_HELP_EX_KEY_LIST] = "  key --list                                          # 列出存储的密钥",
    [TS_STR_SSH_HELP_EX_KEY_GEN]  = "  key --generate --id agx --type rsa                  # 生成 RSA 密钥",
    [TS_STR_SSH_HELP_EX_SSH]  = "  ssh --host 192.168.1.100 --user nvidia --keyid agx --shell",
    [TS_STR_SSH_HELP_EX_COPYID]= "  # 部署公钥到远程服务器 (使用安全存储密钥)",
    [TS_STR_SSH_HELP_EX_COPYID_CMD] = "  ssh --copyid --host 192.168.1.100 --user nvidia --password pw --keyid agx",
    [TS_STR_SSH_HELP_EX_REVOKE]= "  # 从远程服务器撤销已部署的公钥",
    [TS_STR_SSH_HELP_EX_REVOKE_CMD] = "  ssh --revoke --host 192.168.1.100 --user nvidia --password pw --keyid agx",
    [TS_STR_SSH_HELP_EX_FILE] = "  # 或使用基于文件的密钥部署",
    [TS_STR_SSH_HELP_EX_FILE_CMD] = "  ssh --copyid --host 192.168.1.100 --user nvidia --password pw --key /sdcard/id_rsa",
    [TS_STR_SSH_HELP_NOTE]    = "注意: 密钥管理已移至 'key' 命令。使用 'key --help' 查看详情。",
    [TS_STR_SSH_HELP_NOTE_HOSTS] = "      使用 'hosts' 命令管理已知主机。",
};

/* Language names */
static const char *s_language_names[TS_LANG_MAX] = {
    [TS_LANG_EN]    = "English",
    [TS_LANG_ZH_CN] = "简体中文",
};

/* All string tables */
static const char **s_string_tables[TS_LANG_MAX] = {
    [TS_LANG_EN]    = s_strings_en,
    [TS_LANG_ZH_CN] = s_strings_zh_cn,
};

/*===========================================================================*/
/*                              State                                         */
/*===========================================================================*/

static ts_language_t s_current_lang = TS_LANG_EN;
static bool s_initialized = false;

/*===========================================================================*/
/*                              Implementation                                */
/*===========================================================================*/

esp_err_t ts_i18n_init(void)
{
    if (s_initialized) return ESP_OK;
    
    /* Try to load saved language from config */
    int32_t saved_lang = 0;
    if (ts_config_get_int32("system.language", &saved_lang, 0) == ESP_OK) {
        if (saved_lang >= 0 && saved_lang < TS_LANG_MAX) {
            s_current_lang = (ts_language_t)saved_lang;
        }
    }
    
    s_initialized = true;
    TS_LOGI(TAG, "I18n initialized, language: %s", s_language_names[s_current_lang]);
    return ESP_OK;
}

esp_err_t ts_i18n_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_i18n_set_language(ts_language_t lang)
{
    if (lang >= TS_LANG_MAX) return ESP_ERR_INVALID_ARG;
    
    s_current_lang = lang;
    
    /* Save to config */
    ts_config_set_int32("system.language", (int32_t)lang);
    
    TS_LOGI(TAG, "Language set to: %s", s_language_names[lang]);
    return ESP_OK;
}

ts_language_t ts_i18n_get_language(void)
{
    return s_current_lang;
}

const char *ts_i18n_get_language_name(ts_language_t lang)
{
    if (lang >= TS_LANG_MAX) return "Unknown";
    return s_language_names[lang];
}

const char *ts_i18n_get(ts_string_id_t id)
{
    return ts_i18n_get_lang(s_current_lang, id);
}

const char *ts_i18n_get_lang(ts_language_t lang, ts_string_id_t id)
{
    if (id >= TS_STR_MAX) return "???";
    if (lang >= TS_LANG_MAX) lang = TS_LANG_EN;
    
    const char *str = s_string_tables[lang][id];
    
    /* Fallback to English if not available */
    if (str == NULL) {
        str = s_strings_en[id];
    }
    
    /* Final fallback */
    if (str == NULL) {
        str = "???";
    }
    
    return str;
}

int ts_i18n_sprintf(char *buf, size_t size, ts_string_id_t id, ...)
{
    const char *fmt = ts_i18n_get(id);
    
    va_list args;
    va_start(args, id);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    return ret;
}
