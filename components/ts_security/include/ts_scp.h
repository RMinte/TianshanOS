/**
 * @file ts_scp.h
 * @brief SCP Client for simple file transfer
 *
 * This module provides SCP (Secure Copy Protocol) functionality
 * for simple single-file transfers. For directory operations
 * or more complex file management, use SFTP instead.
 */

#pragma once

#include "esp_err.h"
#include "ts_ssh_client.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/** Progress callback for SCP transfers */
typedef void (*ts_scp_progress_cb_t)(uint64_t transferred, uint64_t total, void *user_data);

/*===========================================================================*/
/*                          SCP Transfer Functions                            */
/*===========================================================================*/

/**
 * @brief Send file to remote server via SCP
 *
 * @param ssh_session Connected SSH session
 * @param local_path Local file path to send
 * @param remote_path Remote destination path
 * @param mode File permissions (e.g., 0644), 0 for default
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_scp_send(ts_ssh_session_t ssh_session, const char *local_path,
                       const char *remote_path, int mode,
                       ts_scp_progress_cb_t progress_cb, void *user_data);

/**
 * @brief Receive file from remote server via SCP
 *
 * @param ssh_session Connected SSH session
 * @param remote_path Remote file path to receive
 * @param local_path Local destination path
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_scp_recv(ts_ssh_session_t ssh_session, const char *remote_path,
                       const char *local_path, ts_scp_progress_cb_t progress_cb,
                       void *user_data);

/**
 * @brief Send data buffer to remote file via SCP
 *
 * @param ssh_session Connected SSH session
 * @param buffer Data to send
 * @param size Buffer size
 * @param remote_path Remote destination path
 * @param mode File permissions (e.g., 0644)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_scp_send_buffer(ts_ssh_session_t ssh_session, const uint8_t *buffer,
                              size_t size, const char *remote_path, int mode);

/**
 * @brief Receive remote file to buffer via SCP
 *
 * @param ssh_session Connected SSH session
 * @param remote_path Remote file path
 * @param buffer Pointer to receive allocated buffer (caller must free)
 * @param size Pointer to receive data size
 * @param max_size Maximum allowed size (0 for no limit up to 10MB)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_scp_recv_buffer(ts_ssh_session_t ssh_session, const char *remote_path,
                              uint8_t **buffer, size_t *size, size_t max_size);

#ifdef __cplusplus
}
#endif
