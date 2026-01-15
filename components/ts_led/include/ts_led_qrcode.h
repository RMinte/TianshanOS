/**
 * @file ts_led_qrcode.h
 * @brief QR Code generation for LED matrix display
 * 
 * Provides QR Code generation and display capabilities for 32x32 LED matrices.
 * Uses QR Code versions 1-4 to fit within matrix dimensions.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_LED_QRCODE_H
#define TS_LED_QRCODE_H

#include "esp_err.h"
#include "ts_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief QR Code error correction level
 * 
 * Higher levels provide better error recovery but reduce data capacity.
 */
typedef enum {
    TS_LED_QR_ECC_LOW = 0,       /**< ~7% error recovery */
    TS_LED_QR_ECC_MEDIUM,        /**< ~15% error recovery */
    TS_LED_QR_ECC_QUARTILE,      /**< ~25% error recovery */
    TS_LED_QR_ECC_HIGH           /**< ~30% error recovery */
} ts_led_qr_ecc_t;

/**
 * @brief QR Code display configuration
 */
typedef struct {
    const char *text;            /**< Text to encode (required) */
    ts_led_qr_ecc_t ecc;         /**< Error correction level */
    ts_led_rgb_t fg_color;       /**< Foreground (module) color */
    ts_led_rgb_t bg_color;       /**< Background color */
    int8_t version_min;          /**< Minimum QR version (1-4, 0=auto) */
    int8_t version_max;          /**< Maximum QR version (1-4, 0=auto) */
    bool center;                 /**< Center QR code on matrix */
} ts_led_qr_config_t;

/**
 * @brief QR Code generation result
 */
typedef struct {
    int version;                 /**< Actual QR version used */
    int size;                    /**< QR code size in modules */
    int data_capacity;           /**< Remaining data capacity in bytes */
} ts_led_qr_result_t;

/*===========================================================================*/
/*                              Macros                                        */
/*===========================================================================*/

/**
 * @brief Default QR Code configuration
 * 
 * White QR on black background, medium ECC, auto version selection.
 */
#define TS_LED_QR_DEFAULT_CONFIG() { \
    .text = NULL, \
    .ecc = TS_LED_QR_ECC_MEDIUM, \
    .fg_color = TS_LED_WHITE, \
    .bg_color = TS_LED_BLACK, \
    .version_min = 0, \
    .version_max = 0, \
    .center = true \
}

/** Maximum QR version for 32x32 matrix (33x33 modules) */
#define TS_LED_QR_VERSION_MAX_32x32  4

/** QR Code size formula: version * 4 + 17 */
#define TS_LED_QR_SIZE(version)  ((version) * 4 + 17)

/*===========================================================================*/
/*                              API                                           */
/*===========================================================================*/

/**
 * @brief Display QR Code on LED matrix
 * 
 * Generates a QR Code from the provided text and displays it on the
 * specified layer. For 32x32 matrices, uses QR versions 1-4 (21x21 to 33x33).
 * 
 * QR Version sizes:
 *   - v1: 21x21 (fits in 32x32 with padding)
 *   - v2: 25x25 (fits in 32x32 with padding)
 *   - v3: 29x29 (fits in 32x32 with padding)
 *   - v4: 33x33 (crops 1 pixel on each edge for 32x32)
 * 
 * Data capacity (alphanumeric, Medium ECC):
 *   - v1: 14 chars
 *   - v2: 26 chars
 *   - v3: 42 chars
 *   - v4: 62 chars
 * 
 * @param layer Target layer handle
 * @param config QR Code configuration
 * @param result Optional output for generation result (can be NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if layer or config->text is NULL
 * @return ESP_ERR_INVALID_SIZE if text is too long for max version
 * @return ESP_ERR_NO_MEM if memory allocation failed
 */
esp_err_t ts_led_qrcode_show(ts_led_layer_t layer, 
                              const ts_led_qr_config_t *config,
                              ts_led_qr_result_t *result);

/**
 * @brief Display QR Code on device by name
 * 
 * Convenience function that gets the device by name, obtains layer 0,
 * and displays the QR code.
 * 
 * @param device_name Device name (e.g., "matrix")
 * @param config QR Code configuration
 * @param result Optional output for generation result (can be NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if device not found
 */
esp_err_t ts_led_qrcode_show_on_device(const char *device_name,
                                        const ts_led_qr_config_t *config,
                                        ts_led_qr_result_t *result);

/**
 * @brief Get ECC level name string
 * 
 * @param ecc Error correction level
 * @return Static string name ("L", "M", "Q", "H")
 */
const char *ts_led_qr_ecc_name(ts_led_qr_ecc_t ecc);

/**
 * @brief Parse ECC level from string
 * 
 * @param str String to parse ("L", "M", "Q", "H" or "LOW", "MEDIUM", etc.)
 * @param ecc Output ECC level
 * @return ESP_OK if parsed successfully
 * @return ESP_ERR_INVALID_ARG if string is invalid
 */
esp_err_t ts_led_qr_ecc_parse(const char *str, ts_led_qr_ecc_t *ecc);

/**
 * @brief Get QR Code data capacity for given parameters
 * 
 * Returns the maximum number of characters that can be encoded.
 * 
 * @param version QR version (1-40)
 * @param ecc Error correction level
 * @param is_alphanumeric true for alphanumeric, false for byte mode
 * @return Data capacity in characters, or -1 on error
 */
int ts_led_qr_get_capacity(int version, ts_led_qr_ecc_t ecc, bool is_alphanumeric);

#ifdef __cplusplus
}
#endif

#endif /* TS_LED_QRCODE_H */
