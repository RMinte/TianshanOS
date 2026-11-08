#pragma once
/* External parameter admission. Internal ts_api_call keeps its existing contract. */
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

void ts_api_parse_query(cJSON *params, char *query);

typedef enum {
    TS_API_PARAMS_OK,
    TS_API_PARAMS_INVALID,
    TS_API_PARAMS_NO_MEM
} ts_api_params_status_t;

/* Top-level parameter names are case-insensitive in existing handlers. Nested
 * application data retains case-sensitive names, but exact duplicates are invalid. */
static inline bool ts_api_params_unique(const cJSON *value, unsigned depth)
{
    if (depth > 32 || (cJSON_IsNumber(value) && !isfinite(value->valuedouble))) return false;
    for (const cJSON *a = value->child; a; a = a->next) {
        if (cJSON_IsObject(value)) {
            for (const cJSON *b = a->next; b; b = b->next) {
                if (a->string && b->string &&
                    (depth == 0 ? !strcasecmp(a->string, b->string) : !strcmp(a->string, b->string)))
                    return false;
            }
        }
        if (!ts_api_params_unique(a, depth + 1)) return false;
    }
    return true;
}

static inline cJSON *ts_api_params_parse_object(const char *text, size_t length)
{
    if (!text || !length || memchr(text, 0, length)) return NULL;
    /* Bound parser recursion before cJSON enters it; the existing configuration
     * transport uses the same depth 32. Escaped backslashes stay literal text. */
    bool string = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = text[i];
        if (c < 0x20 && (string || (c != '\t' && c != '\r' && c != '\n'))) return NULL;
        if (string && c == '\\') {
            if (i + 5 < length && text[i+1] == 'u' && !memcmp(text+i+2, "0000", 4)) return NULL;
            ++i;
        } else if (c == '"') string = !string;
        else if (!string && (c == '{' || c == '[')) { if (++depth > 32) return NULL; }
        else if (!string && (c == '}' || c == ']')) { if (!depth) return NULL; --depth; }
    }
    const char *end = NULL;
    cJSON *value = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!value) return NULL;
    while (end < text + length && isspace((unsigned char)*end)) ++end;
    if (end != text + length || !cJSON_IsObject(value) || !ts_api_params_unique(value, 0)) {
        cJSON_Delete(value);
        return NULL;
    }
    return value;
}

/* Keep the existing once-only value decoding and numeric/bool coercion. Reject
 * malformed encodings and ambiguous keys before the legacy parser can lose them. */
static inline ts_api_params_status_t ts_api_params_add_query(cJSON *params, char *query)
{
    char *state = NULL;
    for (char *pair = strtok_r(query, "&", &state); pair; pair = strtok_r(NULL, "&", &state)) {
        char *eq = strchr(pair, '=');
        if (!eq || eq == pair) return TS_API_PARAMS_INVALID;
        for (char *p = pair; *p; ++p) {
            if (*p != '%') continue;
            if (!p[1] || !p[2] || !isxdigit((unsigned char)p[1]) || !isxdigit((unsigned char)p[2]) ||
                (p[1] == '0' && p[2] == '0')) return TS_API_PARAMS_INVALID;
            p += 2;
        }
        *eq = 0;
        bool duplicate = cJSON_GetObjectItem(params, pair) != NULL;
        *eq = '=';
        if (duplicate) return TS_API_PARAMS_INVALID;
        ts_api_parse_query(params, pair);
        /* The parser replaces '=' with NUL. A missing item now means allocation
         * failed; never dispatch a partially populated request. */
        if (!cJSON_GetObjectItemCaseSensitive(params, pair)) return TS_API_PARAMS_NO_MEM;
    }
    return TS_API_PARAMS_OK;
}

#ifdef __cplusplus
}
#endif
