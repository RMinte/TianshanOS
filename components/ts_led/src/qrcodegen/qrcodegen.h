/* 
 * QR Code generator library (C)
 * 
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/*---- Enum and struct types----*/

/* 
 * The error correction level in a QR Code symbol.
 */
enum qrcodegen_Ecc {
    // Must be declared in ascending order of error protection
    qrcodegen_Ecc_LOW = 0,   // The QR Code can tolerate about  7% erroneous codewords
    qrcodegen_Ecc_MEDIUM,    // The QR Code can tolerate about 15% erroneous codewords
    qrcodegen_Ecc_QUARTILE,  // The QR Code can tolerate about 25% erroneous codewords
    qrcodegen_Ecc_HIGH,      // The QR Code can tolerate about 30% erroneous codewords
};


/* 
 * The mask pattern used in a QR Code symbol.
 */
enum qrcodegen_Mask {
    // A special value to tell the QR Code encoder to
    // automatically select an appropriate mask pattern
    qrcodegen_Mask_AUTO = -1,
    // The eight actual mask patterns
    qrcodegen_Mask_0 = 0,
    qrcodegen_Mask_1,
    qrcodegen_Mask_2,
    qrcodegen_Mask_3,
    qrcodegen_Mask_4,
    qrcodegen_Mask_5,
    qrcodegen_Mask_6,
    qrcodegen_Mask_7,
};


/* 
 * Describes how a segment's data bits are interpreted.
 */
enum qrcodegen_Mode {
    qrcodegen_Mode_NUMERIC      = 0x1,
    qrcodegen_Mode_ALPHANUMERIC = 0x2,
    qrcodegen_Mode_BYTE         = 0x4,
    qrcodegen_Mode_KANJI        = 0x8,
    qrcodegen_Mode_ECI          = 0x7,
};


/* 
 * A segment of character/binary/control data in a QR Code symbol.
 */
struct qrcodegen_Segment {
    // The mode indicator of this segment.
    enum qrcodegen_Mode mode;
    
    // The length of this segment's unencoded data.
    int numChars;
    
    // The data bits of this segment, packed in bitwise big endian.
    uint8_t *data;
    
    // The number of valid data bits used in the buffer.
    int bitLength;
};



/*---- Macro constants and functions ----*/

#define qrcodegen_VERSION_MIN   1   // Minimum version number
#define qrcodegen_VERSION_MAX  40   // Maximum version number

// Calculates the number of bytes needed to store any QR Code up to and including the given version number.
#define qrcodegen_BUFFER_LEN_FOR_VERSION(n)  ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)

// The worst-case number of bytes needed to store one QR Code, up to and including version 40.
#define qrcodegen_BUFFER_LEN_MAX  qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)



/*---- Functions (high level) to generate QR Codes ----*/

/* 
 * Encodes the given text string to a QR Code, returning true if successful.
 */
bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t qrcode[],
    enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);


/* 
 * Encodes the given binary data to a QR Code, returning true if successful.
 */
bool qrcodegen_encodeBinary(uint8_t dataAndTemp[], size_t dataLen, uint8_t qrcode[],
    enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);


/*---- Functions (low level) to generate QR Codes ----*/

/* 
 * Encodes the given segments to a QR Code, returning true if successful.
 */
bool qrcodegen_encodeSegments(const struct qrcodegen_Segment segs[], size_t len,
    enum qrcodegen_Ecc ecl, uint8_t tempBuffer[], uint8_t qrcode[]);


/* 
 * Encodes the given segments to a QR Code with advanced options, returning true if successful.
 */
bool qrcodegen_encodeSegmentsAdvanced(const struct qrcodegen_Segment segs[], size_t len, enum qrcodegen_Ecc ecl,
    int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl, uint8_t tempBuffer[], uint8_t qrcode[]);


/* 
 * Tests whether the given string can be encoded as a segment in numeric mode.
 */
bool qrcodegen_isNumeric(const char *text);


/* 
 * Tests whether the given string can be encoded as a segment in alphanumeric mode.
 */
bool qrcodegen_isAlphanumeric(const char *text);


/* 
 * Returns the number of bytes needed for the data buffer of a segment.
 */
size_t qrcodegen_calcSegmentBufferSize(enum qrcodegen_Mode mode, size_t numChars);


/* 
 * Returns a segment representing the given binary data encoded in byte mode.
 */
struct qrcodegen_Segment qrcodegen_makeBytes(const uint8_t data[], size_t len, uint8_t buf[]);


/* 
 * Returns a segment representing the given string of decimal digits encoded in numeric mode.
 */
struct qrcodegen_Segment qrcodegen_makeNumeric(const char *digits, uint8_t buf[]);


/* 
 * Returns a segment representing the given text string encoded in alphanumeric mode.
 */
struct qrcodegen_Segment qrcodegen_makeAlphanumeric(const char *text, uint8_t buf[]);


/* 
 * Returns a segment representing an Extended Channel Interpretation (ECI) designator.
 */
struct qrcodegen_Segment qrcodegen_makeEci(long assignVal, uint8_t buf[]);


/*---- Functions to extract raw data from QR Codes ----*/

/* 
 * Returns the side length of the given QR Code, assuming that encoding succeeded.
 * The result is in the range [21, 177].
 */
int qrcodegen_getSize(const uint8_t qrcode[]);


/* 
 * Returns the color of the module (pixel) at the given coordinates, which is false
 * for light or true for dark. The top left corner has the coordinates (x=0, y=0).
 */
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);


#ifdef __cplusplus
}
#endif
