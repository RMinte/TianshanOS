/**
 * @file ts_ssh_client.c
 * @brief SSH Client Implementation
 * 
 * This provides a lightweight SSH-2 client implementation using mbedtls
 * for cryptographic operations. Currently supports password authentication
 * and basic command execution.
 * 
 * Note: This is a simplified SSH client. For full SSH functionality,
 * consider integrating wolfSSH or libssh2.
 */

#include "ts_ssh_client.h"
#include "ts_log.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/bignum.h>
#include <mbedtls/dhm.h>

#define TAG "ts_ssh"
#define SSH_MAX_PACKET_SIZE 35000
#define SSH_BANNER "SSH-2.0-TianShanOS_0.1\r\n"

/** SSH message types */
typedef enum {
    SSH_MSG_DISCONNECT = 1,
    SSH_MSG_IGNORE = 2,
    SSH_MSG_UNIMPLEMENTED = 3,
    SSH_MSG_DEBUG = 4,
    SSH_MSG_SERVICE_REQUEST = 5,
    SSH_MSG_SERVICE_ACCEPT = 6,
    SSH_MSG_KEXINIT = 20,
    SSH_MSG_NEWKEYS = 21,
    SSH_MSG_KEXDH_INIT = 30,
    SSH_MSG_KEXDH_REPLY = 31,
    SSH_MSG_USERAUTH_REQUEST = 50,
    SSH_MSG_USERAUTH_FAILURE = 51,
    SSH_MSG_USERAUTH_SUCCESS = 52,
    SSH_MSG_USERAUTH_BANNER = 53,
    SSH_MSG_CHANNEL_OPEN = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST = 93,
    SSH_MSG_CHANNEL_DATA = 94,
    SSH_MSG_CHANNEL_EXTENDED_DATA = 95,
    SSH_MSG_CHANNEL_EOF = 96,
    SSH_MSG_CHANNEL_CLOSE = 97,
    SSH_MSG_CHANNEL_REQUEST = 98,
    SSH_MSG_CHANNEL_SUCCESS = 99,
    SSH_MSG_CHANNEL_FAILURE = 100
} ssh_msg_type_t;

/** SSH session structure */
struct ts_ssh_session_s {
    ts_ssh_config_t config;
    ts_ssh_state_t state;
    int socket_fd;
    char error_msg[128];
    
    // Crypto context
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    // Session keys
    uint8_t session_id[32];
    uint8_t iv_c2s[16];
    uint8_t iv_s2c[16];
    uint8_t key_c2s[32];
    uint8_t key_s2c[32];
    uint8_t mac_c2s[32];
    uint8_t mac_s2c[32];
    
    // Packet sequence numbers
    uint32_t seq_c2s;
    uint32_t seq_s2c;
    
    // Encryption enabled
    bool encrypted;
    
    // Channel info
    uint32_t channel_id;
    uint32_t remote_channel;
    uint32_t window_size;
    uint32_t max_packet;
};

static bool s_initialized = false;

// Forward declarations
static esp_err_t ssh_exchange_banners(ts_ssh_session_t session);
static esp_err_t ssh_key_exchange(ts_ssh_session_t session);
static esp_err_t ssh_authenticate(ts_ssh_session_t session);
static esp_err_t ssh_open_channel(ts_ssh_session_t session);
static esp_err_t ssh_exec_command(ts_ssh_session_t session, const char *command);
static esp_err_t ssh_read_response(ts_ssh_session_t session, char **stdout_out, 
                                    size_t *stdout_len, char **stderr_out, 
                                    size_t *stderr_len, int *exit_code);

// Helper functions
static int32_t ssh_read_uint32(const uint8_t *buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static void ssh_write_uint32(uint8_t *buf, uint32_t val)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

static esp_err_t ssh_send_packet(ts_ssh_session_t session, const uint8_t *data, size_t len)
{
    if (!session || session->socket_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // For now, send unencrypted (simplified implementation)
    // Full implementation would encrypt the packet
    
    // Calculate padding
    size_t block_size = 8;  // Minimum block size
    size_t packet_len = len + 1;  // +1 for padding length
    size_t padding = block_size - ((packet_len + 4) % block_size);
    if (padding < 4) padding += block_size;
    
    // Build packet: length(4) + padding_len(1) + payload + padding
    size_t total_len = 4 + 1 + len + padding;
    uint8_t *packet = malloc(total_len);
    if (!packet) return ESP_ERR_NO_MEM;
    
    ssh_write_uint32(packet, 1 + len + padding);
    packet[4] = (uint8_t)padding;
    memcpy(packet + 5, data, len);
    
    // Random padding
    for (size_t i = 0; i < padding; i++) {
        packet[5 + len + i] = (uint8_t)(esp_random() & 0xFF);
    }
    
    ssize_t sent = send(session->socket_fd, packet, total_len, 0);
    free(packet);
    
    if (sent != (ssize_t)total_len) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Send failed: %d", errno);
        return ESP_FAIL;
    }
    
    session->seq_c2s++;
    return ESP_OK;
}

static esp_err_t ssh_recv_packet(ts_ssh_session_t session, uint8_t **data_out, 
                                  size_t *len_out, uint8_t *msg_type)
{
    if (!session || session->socket_fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Read packet length
    uint8_t header[5];
    ssize_t received = recv(session->socket_fd, header, 5, MSG_WAITALL);
    if (received != 5) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Recv header failed: %d", errno);
        return ESP_FAIL;
    }
    
    uint32_t packet_len = ssh_read_uint32(header);
    uint8_t padding_len = header[4];
    
    if (packet_len > SSH_MAX_PACKET_SIZE || packet_len < padding_len + 1) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Invalid packet length: %u", (unsigned)packet_len);
        return ESP_FAIL;
    }
    
    // Read rest of packet
    size_t payload_len = packet_len - padding_len - 1;
    uint8_t *payload = malloc(payload_len);
    if (!payload) return ESP_ERR_NO_MEM;
    
    received = recv(session->socket_fd, payload, payload_len, MSG_WAITALL);
    if (received != (ssize_t)payload_len) {
        free(payload);
        return ESP_FAIL;
    }
    
    // Skip padding
    uint8_t padding[256];
    recv(session->socket_fd, padding, padding_len, MSG_WAITALL);
    
    *msg_type = payload[0];
    *data_out = payload;
    *len_out = payload_len;
    
    session->seq_s2c++;
    return ESP_OK;
}

esp_err_t ts_ssh_client_init(void)
{
    if (s_initialized) return ESP_OK;
    
    s_initialized = true;
    TS_LOGI(TAG, "SSH client initialized");
    return ESP_OK;
}

esp_err_t ts_ssh_client_deinit(void)
{
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_ssh_session_create(const ts_ssh_config_t *config, ts_ssh_session_t *session_out)
{
    if (!config || !config->host || !config->username || !session_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_ssh_session_t session = calloc(1, sizeof(struct ts_ssh_session_s));
    if (!session) {
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(&session->config, config, sizeof(ts_ssh_config_t));
    session->state = TS_SSH_STATE_DISCONNECTED;
    session->socket_fd = -1;
    session->window_size = 0x200000;  // 2MB
    session->max_packet = 0x8000;     // 32KB
    
    // Initialize RNG
    mbedtls_entropy_init(&session->entropy);
    mbedtls_ctr_drbg_init(&session->ctr_drbg);
    
    int ret = mbedtls_ctr_drbg_seed(&session->ctr_drbg, mbedtls_entropy_func,
                                     &session->entropy, 
                                     (const unsigned char*)"ssh_session", 11);
    if (ret != 0) {
        free(session);
        return ESP_FAIL;
    }
    
    *session_out = session;
    return ESP_OK;
}

esp_err_t ts_ssh_session_destroy(ts_ssh_session_t session)
{
    if (!session) return ESP_ERR_INVALID_ARG;
    
    ts_ssh_disconnect(session);
    
    mbedtls_ctr_drbg_free(&session->ctr_drbg);
    mbedtls_entropy_free(&session->entropy);
    
    free(session);
    return ESP_OK;
}

esp_err_t ts_ssh_connect(ts_ssh_session_t session)
{
    if (!session) return ESP_ERR_INVALID_ARG;
    
    if (session->state != TS_SSH_STATE_DISCONNECTED) {
        snprintf(session->error_msg, sizeof(session->error_msg), "Already connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    session->state = TS_SSH_STATE_CONNECTING;
    
    // Resolve hostname
    struct hostent *he = gethostbyname(session->config.host);
    if (!he) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "DNS resolution failed");
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }
    
    // Create socket
    session->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (session->socket_fd < 0) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Socket creation failed");
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = session->config.timeout_ms / 1000;
    tv.tv_usec = (session->config.timeout_ms % 1000) * 1000;
    setsockopt(session->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(session->socket_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->config.port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    if (connect(session->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Connection failed: %d", errno);
        close(session->socket_fd);
        session->socket_fd = -1;
        session->state = TS_SSH_STATE_ERROR;
        return ESP_FAIL;
    }
    
    TS_LOGI(TAG, "Connected to %s:%d", session->config.host, session->config.port);
    
    // Exchange banners
    esp_err_t ret = ssh_exchange_banners(session);
    if (ret != ESP_OK) {
        ts_ssh_disconnect(session);
        return ret;
    }
    
    // Key exchange (simplified - actual impl needs DH/ECDH)
    ret = ssh_key_exchange(session);
    if (ret != ESP_OK) {
        ts_ssh_disconnect(session);
        return ret;
    }
    
    // Authenticate
    session->state = TS_SSH_STATE_AUTHENTICATING;
    ret = ssh_authenticate(session);
    if (ret != ESP_OK) {
        ts_ssh_disconnect(session);
        return ret;
    }
    
    session->state = TS_SSH_STATE_CONNECTED;
    TS_LOGI(TAG, "SSH session authenticated");
    
    return ESP_OK;
}

esp_err_t ts_ssh_disconnect(ts_ssh_session_t session)
{
    if (!session) return ESP_ERR_INVALID_ARG;
    
    if (session->socket_fd >= 0) {
        // Send disconnect message
        uint8_t disconnect_msg[16];
        disconnect_msg[0] = SSH_MSG_DISCONNECT;
        ssh_write_uint32(disconnect_msg + 1, 11);  // SSH_DISCONNECT_BY_APPLICATION
        ssh_write_uint32(disconnect_msg + 5, 0);   // description len
        ssh_write_uint32(disconnect_msg + 9, 0);   // language tag len
        
        ssh_send_packet(session, disconnect_msg, 13);
        
        close(session->socket_fd);
        session->socket_fd = -1;
    }
    
    session->state = TS_SSH_STATE_DISCONNECTED;
    session->encrypted = false;
    session->seq_c2s = 0;
    session->seq_s2c = 0;
    
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

esp_err_t ts_ssh_exec(ts_ssh_session_t session, const char *command,
                       ts_ssh_exec_result_t *result)
{
    if (!session || !command || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (session->state != TS_SSH_STATE_CONNECTED) {
        snprintf(session->error_msg, sizeof(session->error_msg), "Not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(result, 0, sizeof(*result));
    
    // Open session channel
    esp_err_t ret = ssh_open_channel(session);
    if (ret != ESP_OK) return ret;
    
    // Execute command
    ret = ssh_exec_command(session, command);
    if (ret != ESP_OK) return ret;
    
    // Read response
    ret = ssh_read_response(session, &result->stdout_data, &result->stdout_len,
                            &result->stderr_data, &result->stderr_len,
                            &result->exit_code);
    
    return ret;
}

esp_err_t ts_ssh_exec_stream(ts_ssh_session_t session, const char *command,
                              ts_ssh_output_cb_t callback, void *user_data,
                              int *exit_code)
{
    // Simplified: just use regular exec and call callback
    ts_ssh_exec_result_t result;
    esp_err_t ret = ts_ssh_exec(session, command, &result);
    
    if (ret == ESP_OK && callback) {
        if (result.stdout_data && result.stdout_len > 0) {
            callback(result.stdout_data, result.stdout_len, false, user_data);
        }
        if (result.stderr_data && result.stderr_len > 0) {
            callback(result.stderr_data, result.stderr_len, true, user_data);
        }
    }
    
    if (exit_code) *exit_code = result.exit_code;
    ts_ssh_exec_result_free(&result);
    
    return ret;
}

void ts_ssh_exec_result_free(ts_ssh_exec_result_t *result)
{
    if (!result) return;
    free(result->stdout_data);
    free(result->stderr_data);
    memset(result, 0, sizeof(*result));
}

const char *ts_ssh_get_error(ts_ssh_session_t session)
{
    return session ? session->error_msg : "Invalid session";
}

esp_err_t ts_ssh_exec_simple(const ts_ssh_config_t *config, const char *command,
                              ts_ssh_exec_result_t *result)
{
    ts_ssh_session_t session = NULL;
    
    esp_err_t ret = ts_ssh_session_create(config, &session);
    if (ret != ESP_OK) return ret;
    
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

// Internal functions

static esp_err_t ssh_exchange_banners(ts_ssh_session_t session)
{
    // Send our banner
    if (send(session->socket_fd, SSH_BANNER, strlen(SSH_BANNER), 0) < 0) {
        snprintf(session->error_msg, sizeof(session->error_msg), "Banner send failed");
        return ESP_FAIL;
    }
    
    // Receive server banner
    char banner[256];
    size_t pos = 0;
    while (pos < sizeof(banner) - 1) {
        ssize_t n = recv(session->socket_fd, banner + pos, 1, 0);
        if (n <= 0) break;
        pos++;
        if (pos >= 2 && banner[pos-2] == '\r' && banner[pos-1] == '\n') {
            banner[pos] = '\0';
            break;
        }
    }
    
    if (strncmp(banner, "SSH-2.0-", 8) != 0) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Invalid server banner: %.64s", banner);
        return ESP_FAIL;
    }
    
    TS_LOGD(TAG, "Server banner: %s", banner);
    return ESP_OK;
}

static esp_err_t ssh_key_exchange(ts_ssh_session_t session)
{
    // Build KEXINIT packet
    uint8_t kexinit[512];
    size_t pos = 0;
    
    kexinit[pos++] = SSH_MSG_KEXINIT;
    
    // Cookie (16 random bytes)
    for (int i = 0; i < 16; i++) {
        kexinit[pos++] = (uint8_t)(esp_random() & 0xFF);
    }
    
    // Algorithm lists (simplified - use common algorithms)
    const char *kex_algs = "diffie-hellman-group14-sha256";
    const char *host_key_algs = "ssh-rsa";
    const char *enc_algs = "aes128-ctr";
    const char *mac_algs = "hmac-sha2-256";
    const char *comp_algs = "none";
    const char *empty = "";
    
    // Helper to write string
    #define WRITE_STRING(s) do { \
        size_t slen = strlen(s); \
        ssh_write_uint32(kexinit + pos, slen); pos += 4; \
        memcpy(kexinit + pos, s, slen); pos += slen; \
    } while(0)
    
    WRITE_STRING(kex_algs);         // kex_algorithms
    WRITE_STRING(host_key_algs);    // server_host_key_algorithms
    WRITE_STRING(enc_algs);         // encryption_algorithms_client_to_server
    WRITE_STRING(enc_algs);         // encryption_algorithms_server_to_client
    WRITE_STRING(mac_algs);         // mac_algorithms_client_to_server
    WRITE_STRING(mac_algs);         // mac_algorithms_server_to_client
    WRITE_STRING(comp_algs);        // compression_algorithms_client_to_server
    WRITE_STRING(comp_algs);        // compression_algorithms_server_to_client
    WRITE_STRING(empty);            // languages_client_to_server
    WRITE_STRING(empty);            // languages_server_to_client
    
    #undef WRITE_STRING
    
    kexinit[pos++] = 0;  // first_kex_packet_follows
    ssh_write_uint32(kexinit + pos, 0);  // reserved
    pos += 4;
    
    // Send KEXINIT
    esp_err_t ret = ssh_send_packet(session, kexinit, pos);
    if (ret != ESP_OK) return ret;
    
    // Receive server KEXINIT
    uint8_t *server_kexinit;
    size_t kexinit_len;
    uint8_t msg_type;
    
    ret = ssh_recv_packet(session, &server_kexinit, &kexinit_len, &msg_type);
    if (ret != ESP_OK) return ret;
    
    if (msg_type != SSH_MSG_KEXINIT) {
        free(server_kexinit);
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Expected KEXINIT, got %d", msg_type);
        return ESP_FAIL;
    }
    free(server_kexinit);
    
    // Note: Full DH key exchange would happen here
    // For this simplified implementation, we'll skip to authentication
    // This means encryption won't be properly established
    
    // Send NEWKEYS
    uint8_t newkeys = SSH_MSG_NEWKEYS;
    ret = ssh_send_packet(session, &newkeys, 1);
    if (ret != ESP_OK) return ret;
    
    // Receive NEWKEYS
    uint8_t *newkeys_pkt;
    size_t newkeys_len;
    ret = ssh_recv_packet(session, &newkeys_pkt, &newkeys_len, &msg_type);
    if (ret != ESP_OK) return ret;
    free(newkeys_pkt);
    
    if (msg_type != SSH_MSG_NEWKEYS) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Expected NEWKEYS, got %d", msg_type);
        return ESP_FAIL;
    }
    
    TS_LOGD(TAG, "Key exchange completed");
    return ESP_OK;
}

static esp_err_t ssh_authenticate(ts_ssh_session_t session)
{
    // Request ssh-userauth service
    uint8_t service_req[64];
    size_t pos = 0;
    
    service_req[pos++] = SSH_MSG_SERVICE_REQUEST;
    const char *service = "ssh-userauth";
    ssh_write_uint32(service_req + pos, strlen(service));
    pos += 4;
    memcpy(service_req + pos, service, strlen(service));
    pos += strlen(service);
    
    esp_err_t ret = ssh_send_packet(session, service_req, pos);
    if (ret != ESP_OK) return ret;
    
    // Receive service accept
    uint8_t *response;
    size_t resp_len;
    uint8_t msg_type;
    
    ret = ssh_recv_packet(session, &response, &resp_len, &msg_type);
    if (ret != ESP_OK) return ret;
    free(response);
    
    if (msg_type != SSH_MSG_SERVICE_ACCEPT) {
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Service request rejected");
        return ESP_FAIL;
    }
    
    // Password authentication
    if (session->config.auth_method == TS_SSH_AUTH_PASSWORD) {
        uint8_t auth_req[256];
        pos = 0;
        
        auth_req[pos++] = SSH_MSG_USERAUTH_REQUEST;
        
        // Username
        size_t ulen = strlen(session->config.username);
        ssh_write_uint32(auth_req + pos, ulen);
        pos += 4;
        memcpy(auth_req + pos, session->config.username, ulen);
        pos += ulen;
        
        // Service name
        const char *svc = "ssh-connection";
        ssh_write_uint32(auth_req + pos, strlen(svc));
        pos += 4;
        memcpy(auth_req + pos, svc, strlen(svc));
        pos += strlen(svc);
        
        // Method name
        const char *method = "password";
        ssh_write_uint32(auth_req + pos, strlen(method));
        pos += 4;
        memcpy(auth_req + pos, method, strlen(method));
        pos += strlen(method);
        
        // False (not changing password)
        auth_req[pos++] = 0;
        
        // Password
        size_t plen = strlen(session->config.auth.password);
        ssh_write_uint32(auth_req + pos, plen);
        pos += 4;
        memcpy(auth_req + pos, session->config.auth.password, plen);
        pos += plen;
        
        ret = ssh_send_packet(session, auth_req, pos);
        if (ret != ESP_OK) return ret;
        
        // Check response
        ret = ssh_recv_packet(session, &response, &resp_len, &msg_type);
        if (ret != ESP_OK) return ret;
        free(response);
        
        if (msg_type == SSH_MSG_USERAUTH_SUCCESS) {
            return ESP_OK;
        } else if (msg_type == SSH_MSG_USERAUTH_FAILURE) {
            snprintf(session->error_msg, sizeof(session->error_msg), 
                     "Authentication failed");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    snprintf(session->error_msg, sizeof(session->error_msg), 
             "Unsupported auth method");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t ssh_open_channel(ts_ssh_session_t session)
{
    uint8_t open_req[64];
    size_t pos = 0;
    
    open_req[pos++] = SSH_MSG_CHANNEL_OPEN;
    
    // Channel type
    const char *type = "session";
    ssh_write_uint32(open_req + pos, strlen(type));
    pos += 4;
    memcpy(open_req + pos, type, strlen(type));
    pos += strlen(type);
    
    // Sender channel
    session->channel_id = 0;
    ssh_write_uint32(open_req + pos, session->channel_id);
    pos += 4;
    
    // Initial window size
    ssh_write_uint32(open_req + pos, session->window_size);
    pos += 4;
    
    // Maximum packet size
    ssh_write_uint32(open_req + pos, session->max_packet);
    pos += 4;
    
    esp_err_t ret = ssh_send_packet(session, open_req, pos);
    if (ret != ESP_OK) return ret;
    
    // Receive confirmation
    uint8_t *response;
    size_t resp_len;
    uint8_t msg_type;
    
    ret = ssh_recv_packet(session, &response, &resp_len, &msg_type);
    if (ret != ESP_OK) return ret;
    
    if (msg_type == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
        session->remote_channel = ssh_read_uint32(response + 1);
        free(response);
        return ESP_OK;
    } else if (msg_type == SSH_MSG_CHANNEL_OPEN_FAILURE) {
        free(response);
        snprintf(session->error_msg, sizeof(session->error_msg), 
                 "Channel open failed");
        return ESP_FAIL;
    }
    
    free(response);
    return ESP_FAIL;
}

static esp_err_t ssh_exec_command(ts_ssh_session_t session, const char *command)
{
    uint8_t exec_req[1024];
    size_t pos = 0;
    
    exec_req[pos++] = SSH_MSG_CHANNEL_REQUEST;
    
    // Recipient channel
    ssh_write_uint32(exec_req + pos, session->remote_channel);
    pos += 4;
    
    // Request type
    const char *type = "exec";
    ssh_write_uint32(exec_req + pos, strlen(type));
    pos += 4;
    memcpy(exec_req + pos, type, strlen(type));
    pos += strlen(type);
    
    // Want reply
    exec_req[pos++] = 1;
    
    // Command
    size_t cmd_len = strlen(command);
    ssh_write_uint32(exec_req + pos, cmd_len);
    pos += 4;
    memcpy(exec_req + pos, command, cmd_len);
    pos += cmd_len;
    
    esp_err_t ret = ssh_send_packet(session, exec_req, pos);
    if (ret != ESP_OK) return ret;
    
    // Wait for success/failure
    uint8_t *response;
    size_t resp_len;
    uint8_t msg_type;
    
    ret = ssh_recv_packet(session, &response, &resp_len, &msg_type);
    if (ret != ESP_OK) return ret;
    free(response);
    
    if (msg_type == SSH_MSG_CHANNEL_SUCCESS) {
        return ESP_OK;
    }
    
    snprintf(session->error_msg, sizeof(session->error_msg), 
             "Command execution failed");
    return ESP_FAIL;
}

static esp_err_t ssh_read_response(ts_ssh_session_t session, char **stdout_out,
                                    size_t *stdout_len, char **stderr_out,
                                    size_t *stderr_len, int *exit_code)
{
    // Buffers for output
    char *stdout_buf = malloc(4096);
    char *stderr_buf = malloc(4096);
    size_t stdout_pos = 0, stderr_pos = 0;
    size_t stdout_cap = 4096, stderr_cap = 4096;
    
    if (!stdout_buf || !stderr_buf) {
        free(stdout_buf);
        free(stderr_buf);
        return ESP_ERR_NO_MEM;
    }
    
    *exit_code = -1;
    bool channel_closed = false;
    
    while (!channel_closed) {
        uint8_t *response;
        size_t resp_len;
        uint8_t msg_type;
        
        esp_err_t ret = ssh_recv_packet(session, &response, &resp_len, &msg_type);
        if (ret != ESP_OK) break;
        
        switch (msg_type) {
            case SSH_MSG_CHANNEL_DATA: {
                // Skip channel id (4 bytes) + string length (4 bytes)
                if (resp_len > 9) {
                    size_t data_len = ssh_read_uint32(response + 5);
                    const char *data = (const char*)(response + 9);
                    
                    // Expand buffer if needed
                    while (stdout_pos + data_len > stdout_cap) {
                        stdout_cap *= 2;
                        stdout_buf = realloc(stdout_buf, stdout_cap);
                    }
                    
                    memcpy(stdout_buf + stdout_pos, data, data_len);
                    stdout_pos += data_len;
                }
                break;
            }
            case SSH_MSG_CHANNEL_EXTENDED_DATA: {
                // Skip channel id (4) + data type (4) + string length (4)
                if (resp_len > 13) {
                    size_t data_len = ssh_read_uint32(response + 9);
                    const char *data = (const char*)(response + 13);
                    
                    while (stderr_pos + data_len > stderr_cap) {
                        stderr_cap *= 2;
                        stderr_buf = realloc(stderr_buf, stderr_cap);
                    }
                    
                    memcpy(stderr_buf + stderr_pos, data, data_len);
                    stderr_pos += data_len;
                }
                break;
            }
            case SSH_MSG_CHANNEL_REQUEST: {
                // Check for exit-status
                if (resp_len > 17) {
                    size_t type_len = ssh_read_uint32(response + 5);
                    if (type_len == 11 && memcmp(response + 9, "exit-status", 11) == 0) {
                        *exit_code = ssh_read_uint32(response + 21);
                    }
                }
                break;
            }
            case SSH_MSG_CHANNEL_EOF:
            case SSH_MSG_CHANNEL_CLOSE:
                channel_closed = true;
                break;
                
            default:
                break;
        }
        
        free(response);
    }
    
    // Send channel close if server didn't
    uint8_t close_msg[5];
    close_msg[0] = SSH_MSG_CHANNEL_CLOSE;
    ssh_write_uint32(close_msg + 1, session->remote_channel);
    ssh_send_packet(session, close_msg, 5);
    
    // Set outputs
    *stdout_out = stdout_buf;
    *stdout_len = stdout_pos;
    *stderr_out = stderr_buf;
    *stderr_len = stderr_pos;
    
    return ESP_OK;
}
