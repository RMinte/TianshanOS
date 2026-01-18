/**
 * @file ts_port_forward.h
 * @brief SSH Port Forwarding (Tunneling) API
 * 
 * This module provides local and remote port forwarding capabilities,
 * allowing secure tunneling of TCP connections through SSH.
 * 
 * Use cases:
 * - Access internal services on remote network (local forwarding)
 * - Expose local services to remote network (remote forwarding)
 * - Secure database/API access through SSH tunnel
 */

#pragma once

#include "esp_err.h"
#include "ts_ssh_client.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Port forwarding direction */
typedef enum {
    TS_FORWARD_LOCAL = 0,   /**< Local -> Remote (direct-tcpip) */
    TS_FORWARD_REMOTE       /**< Remote -> Local (tcpip-forward) */
} ts_forward_direction_t;

/** Port forwarding state */
typedef enum {
    TS_FORWARD_STATE_IDLE = 0,
    TS_FORWARD_STATE_RUNNING,
    TS_FORWARD_STATE_STOPPED,
    TS_FORWARD_STATE_ERROR
} ts_forward_state_t;

/** Port forwarding handle */
typedef struct ts_port_forward_s *ts_port_forward_t;

/** Port forwarding configuration */
typedef struct {
    ts_forward_direction_t direction;   /**< Forwarding direction */
    
    /* Local endpoint (where clients connect for local forward) */
    const char *local_host;             /**< Local bind address (default: "127.0.0.1") */
    uint16_t local_port;                /**< Local port to listen on */
    
    /* Remote endpoint (where connections are forwarded to) */
    const char *remote_host;            /**< Remote host to connect to */
    uint16_t remote_port;               /**< Remote port to connect to */
    
    /* Options */
    uint32_t timeout_ms;                /**< I/O timeout in ms (default: 5000) */
    size_t buffer_size;                 /**< Transfer buffer size (default: 4096) */
    int max_connections;                /**< Max concurrent connections (default: 5) */
} ts_forward_config_t;

/** Default port forwarding configuration */
#define TS_FORWARD_DEFAULT_CONFIG() { \
    .direction = TS_FORWARD_LOCAL, \
    .local_host = "127.0.0.1", \
    .local_port = 0, \
    .remote_host = NULL, \
    .remote_port = 0, \
    .timeout_ms = 5000, \
    .buffer_size = 4096, \
    .max_connections = 5 \
}

/** Port forwarding statistics */
typedef struct {
    uint64_t bytes_sent;            /**< Total bytes sent through tunnel */
    uint64_t bytes_received;        /**< Total bytes received from tunnel */
    uint32_t active_connections;    /**< Current active connections */
    uint32_t total_connections;     /**< Total connections handled */
    ts_forward_state_t state;       /**< Current state */
} ts_forward_stats_t;

/** Connection callback (called when new connection established) */
typedef void (*ts_forward_connect_cb_t)(ts_port_forward_t forward, 
                                        const char *client_addr, 
                                        uint16_t client_port,
                                        void *user_data);

/** Disconnection callback */
typedef void (*ts_forward_disconnect_cb_t)(ts_port_forward_t forward,
                                           uint64_t bytes_transferred,
                                           void *user_data);

/**
 * @brief Create a port forwarding tunnel
 * 
 * @param session SSH session (must be connected)
 * @param config Forwarding configuration
 * @param forward_out Pointer to receive forward handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_create(ts_ssh_session_t session,
                                  const ts_forward_config_t *config,
                                  ts_port_forward_t *forward_out);

/**
 * @brief Start port forwarding (runs in background task)
 * 
 * For local forwarding, this starts a TCP listener on local_host:local_port
 * that forwards connections to remote_host:remote_port via SSH tunnel.
 * 
 * @param forward Forward handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_start(ts_port_forward_t forward);

/**
 * @brief Stop port forwarding
 * 
 * @param forward Forward handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_stop(ts_port_forward_t forward);

/**
 * @brief Destroy port forwarding handle
 * 
 * Automatically stops forwarding if running.
 * 
 * @param forward Forward handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_destroy(ts_port_forward_t forward);

/**
 * @brief Get port forwarding statistics
 * 
 * @param forward Forward handle
 * @param stats Pointer to receive statistics
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_get_stats(ts_port_forward_t forward, 
                                     ts_forward_stats_t *stats);

/**
 * @brief Get port forwarding state
 * 
 * @param forward Forward handle
 * @return ts_forward_state_t Current state
 */
ts_forward_state_t ts_port_forward_get_state(ts_port_forward_t forward);

/**
 * @brief Set connection callback
 * 
 * @param forward Forward handle
 * @param cb Callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_set_connect_cb(ts_port_forward_t forward,
                                          ts_forward_connect_cb_t cb,
                                          void *user_data);

/**
 * @brief Set disconnection callback
 * 
 * @param forward Forward handle
 * @param cb Callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_set_disconnect_cb(ts_port_forward_t forward,
                                             ts_forward_disconnect_cb_t cb,
                                             void *user_data);

/**
 * @brief Perform single-shot port forwarding (blocking)
 * 
 * Creates a direct TCP connection to remote_host:remote_port via SSH tunnel,
 * and proxies data until connection closes or timeout.
 * 
 * This is useful for simple one-off connections without running a listener.
 * 
 * @param session SSH session
 * @param remote_host Remote host to connect to
 * @param remote_port Remote port to connect to
 * @param local_sock Local socket to proxy data to/from
 * @param timeout_ms Idle timeout in ms (0 = no timeout)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_port_forward_direct(ts_ssh_session_t session,
                                  const char *remote_host,
                                  uint16_t remote_port,
                                  int local_sock,
                                  uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
