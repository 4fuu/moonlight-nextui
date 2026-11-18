#ifndef JSON_LITE_H
#define JSON_LITE_H

#include <stddef.h>

/*
 * Small strict JSON scanner shared by the device protocol and the device grant
 * store. It validates complete JSON values but materializes only the strings
 * the caller asks for. No dependencies beyond libc, matching the hand-written
 * scanner already used by pak_manifest.c and ra_util.h.
 *
 * Strings are decoded to UTF-8; embedded NUL is rejected because callers store
 * results in C strings. Escapes are fully validated.
 */

#define JSON_LITE_MAX_DEPTH 32

typedef struct
{
    const char* p;
    const char* end;
} JsonLiteCursor;

void json_lite_init(JsonLiteCursor* cursor, const char* text, size_t length);
void json_lite_skip_ws(JsonLiteCursor* cursor);

/*
 * Parse one JSON string. out_len receives the full decoded length even when the
 * destination is too small; callers treat out_len >= out_size as overlong.
 * out may be NULL to validate and measure only.
 */
int json_lite_parse_string(JsonLiteCursor* cursor, char* out, size_t out_size, size_t* out_len);

/* Validate and skip one JSON value. Returns 0 on malformed input. */
int json_lite_skip_value(JsonLiteCursor* cursor, int depth);

/* Integer-only number (no fraction or exponent); 0 on overflow or other syntax. */
int json_lite_parse_integer(JsonLiteCursor* cursor, long* out);

/*
 * Validate one JSON value and report its raw span (leading whitespace already
 * skipped by the caller). Used to forward an opaque "args" value.
 */
int json_lite_capture_value(JsonLiteCursor* cursor, const char** start, size_t* length);

/*
 * Escape text as a JSON string body (quotes are written by the caller). Returns
 * 1 and NUL-terminates when the destination fits, 0 otherwise.
 */
int json_lite_escape(const char* text, char* out, size_t out_size);

#endif
