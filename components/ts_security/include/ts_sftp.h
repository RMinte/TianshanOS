/**
 * @file ts_sftp.h
 * @brief SFTP Client for secure file transfer
 *
 * This module provides SFTP functionality for TianShanOS,
 * enabling secure file transfer to/from remote devices.
 */

#pragma once

#include "esp_err.h"
#include "ts_ssh_client.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/** SFTP session handle */
typedef struct ts_sftp_session_s *ts_sftp_session_t;

/** SFTP file handle */
typedef struct ts_sftp_file_s *ts_sftp_file_t;

/** SFTP directory handle */
typedef struct ts_sftp_dir_s *ts_sftp_dir_t;

/** File open flags */
typedef enum {
    TS_SFTP_READ    = 0x01,     /**< Open for reading */
    TS_SFTP_WRITE   = 0x02,     /**< Open for writing */
    TS_SFTP_APPEND  = 0x04,     /**< Append mode */
    TS_SFTP_CREATE  = 0x08,     /**< Create if not exists */
    TS_SFTP_TRUNC   = 0x10,     /**< Truncate if exists */
    TS_SFTP_EXCL    = 0x20,     /**< Fail if exists (with CREATE) */
} ts_sftp_open_flags_t;

/** File attributes */
typedef struct {
    uint64_t size;              /**< File size in bytes */
    uint32_t uid;               /**< User ID */
    uint32_t gid;               /**< Group ID */
    uint32_t permissions;       /**< File permissions (POSIX) */
    uint32_t atime;             /**< Last access time */
    uint32_t mtime;             /**< Last modification time */
    bool is_dir;                /**< Is directory */
    bool is_link;               /**< Is symbolic link */
} ts_sftp_attr_t;

/** Directory entry */
typedef struct {
    char name[256];             /**< File/directory name */
    ts_sftp_attr_t attrs;       /**< File attributes */
} ts_sftp_dirent_t;

/** Progress callback for file transfers */
typedef void (*ts_sftp_progress_cb_t)(uint64_t transferred, uint64_t total, void *user_data);

/*===========================================================================*/
/*                          Session Management                                */
/*===========================================================================*/

/**
 * @brief Open SFTP session over existing SSH connection
 *
 * @param ssh_session Existing connected SSH session
 * @param sftp_out Pointer to receive SFTP session handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_open(ts_ssh_session_t ssh_session, ts_sftp_session_t *sftp_out);

/**
 * @brief Close SFTP session
 *
 * @param sftp SFTP session handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_close(ts_sftp_session_t sftp);

/**
 * @brief Get last SFTP error message
 *
 * @param sftp SFTP session handle
 * @return Error message string
 */
const char *ts_sftp_get_error(ts_sftp_session_t sftp);

/*===========================================================================*/
/*                          File Operations                                   */
/*===========================================================================*/

/**
 * @brief Open remote file
 *
 * @param sftp SFTP session handle
 * @param path Remote file path
 * @param flags Open flags (TS_SFTP_READ, TS_SFTP_WRITE, etc.)
 * @param mode File permissions for new files (e.g., 0644)
 * @param file_out Pointer to receive file handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_file_open(ts_sftp_session_t sftp, const char *path,
                            int flags, int mode, ts_sftp_file_t *file_out);

/**
 * @brief Close remote file
 *
 * @param file File handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_file_close(ts_sftp_file_t file);

/**
 * @brief Read from remote file
 *
 * @param file File handle
 * @param buffer Buffer to store data
 * @param size Maximum bytes to read
 * @param bytes_read Actual bytes read
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND on EOF
 */
esp_err_t ts_sftp_file_read(ts_sftp_file_t file, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Write to remote file
 *
 * @param file File handle
 * @param buffer Data to write
 * @param size Bytes to write
 * @param bytes_written Actual bytes written
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_file_write(ts_sftp_file_t file, const void *buffer, size_t size, size_t *bytes_written);

/**
 * @brief Seek in remote file
 *
 * @param file File handle
 * @param offset Offset in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_file_seek(ts_sftp_file_t file, uint64_t offset);

/**
 * @brief Get file attributes
 *
 * @param sftp SFTP session handle
 * @param path Remote file path
 * @param attrs Pointer to store attributes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_stat(ts_sftp_session_t sftp, const char *path, ts_sftp_attr_t *attrs);

/**
 * @brief Delete remote file
 *
 * @param sftp SFTP session handle
 * @param path Remote file path
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_unlink(ts_sftp_session_t sftp, const char *path);

/**
 * @brief Rename remote file
 *
 * @param sftp SFTP session handle
 * @param old_path Current path
 * @param new_path New path
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_rename(ts_sftp_session_t sftp, const char *old_path, const char *new_path);

/*===========================================================================*/
/*                        Directory Operations                                */
/*===========================================================================*/

/**
 * @brief Open remote directory
 *
 * @param sftp SFTP session handle
 * @param path Remote directory path
 * @param dir_out Pointer to receive directory handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_dir_open(ts_sftp_session_t sftp, const char *path, ts_sftp_dir_t *dir_out);

/**
 * @brief Read next directory entry
 *
 * @param dir Directory handle
 * @param entry Pointer to store entry
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND when no more entries
 */
esp_err_t ts_sftp_dir_read(ts_sftp_dir_t dir, ts_sftp_dirent_t *entry);

/**
 * @brief Close directory handle
 *
 * @param dir Directory handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_dir_close(ts_sftp_dir_t dir);

/**
 * @brief Create remote directory
 *
 * @param sftp SFTP session handle
 * @param path Remote directory path
 * @param mode Directory permissions (e.g., 0755)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_mkdir(ts_sftp_session_t sftp, const char *path, int mode);

/**
 * @brief Remove remote directory
 *
 * @param sftp SFTP session handle
 * @param path Remote directory path
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_rmdir(ts_sftp_session_t sftp, const char *path);

/*===========================================================================*/
/*                      High-Level Transfer Functions                         */
/*===========================================================================*/

/**
 * @brief Download remote file to local path
 *
 * @param sftp SFTP session handle
 * @param remote_path Remote file path
 * @param local_path Local file path (SD card or SPIFFS)
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_get(ts_sftp_session_t sftp, const char *remote_path,
                       const char *local_path, ts_sftp_progress_cb_t progress_cb,
                       void *user_data);

/**
 * @brief Upload local file to remote path
 *
 * @param sftp SFTP session handle
 * @param local_path Local file path
 * @param remote_path Remote file path
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_put(ts_sftp_session_t sftp, const char *local_path,
                       const char *remote_path, ts_sftp_progress_cb_t progress_cb,
                       void *user_data);

/**
 * @brief Download remote file to memory buffer
 *
 * @param sftp SFTP session handle
 * @param remote_path Remote file path
 * @param buffer Pointer to receive allocated buffer (caller must free)
 * @param size Pointer to receive buffer size
 * @param max_size Maximum allowed size (0 for no limit)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_get_to_buffer(ts_sftp_session_t sftp, const char *remote_path,
                                 uint8_t **buffer, size_t *size, size_t max_size);

/**
 * @brief Upload memory buffer to remote file
 *
 * @param sftp SFTP session handle
 * @param buffer Data buffer
 * @param size Buffer size
 * @param remote_path Remote file path
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_sftp_put_from_buffer(ts_sftp_session_t sftp, const uint8_t *buffer,
                                   size_t size, const char *remote_path);

#ifdef __cplusplus
}
#endif
