#include "json_lite.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

void json_lite_init(JsonLiteCursor* cursor, const char* text, size_t length)
{
    cursor->p = text;
    cursor->end = text + length;
}

void json_lite_skip_ws(JsonLiteCursor* cursor)
{
    while (cursor->p < cursor->end)
    {
        char ch = *cursor->p;
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
        cursor->p++;
    }
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int parse_hex4(JsonLiteCursor* cursor, unsigned* out)
{
    unsigned value = 0;
    int i;

    if ((size_t)(cursor->end - cursor->p) < 4) return 0;
    for (i = 0; i < 4; i++)
    {
        int digit = hex_value(cursor->p[i]);
        if (digit < 0) return 0;
        value = (value << 4) | (unsigned)digit;
    }
    cursor->p += 4;
    *out = value;
    return 1;
}

static void append_byte(char* out, size_t out_size, size_t* written, unsigned char ch)
{
    if (out != NULL && out_size > 0 && *written < out_size - 1) out[*written] = (char)ch;
    (*written)++;
}

static int append_utf8(char* out, size_t out_size, size_t* written, unsigned codepoint)
{
    unsigned char bytes[4];
    size_t length;
    size_t i;

    if (codepoint <= 0x7Fu)
    {
        bytes[0] = (unsigned char)codepoint;
        length = 1;
    }
    else if (codepoint <= 0x7FFu)
    {
        bytes[0] = (unsigned char)(0xC0u | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80u | (codepoint & 0x3Fu));
        length = 2;
    }
    else if (codepoint <= 0xFFFFu)
    {
        bytes[0] = (unsigned char)(0xE0u | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        bytes[2] = (unsigned char)(0x80u | (codepoint & 0x3Fu));
        length = 3;
    }
    else if (codepoint <= 0x10FFFFu)
    {
        bytes[0] = (unsigned char)(0xF0u | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        bytes[2] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        bytes[3] = (unsigned char)(0x80u | (codepoint & 0x3Fu));
        length = 4;
    }
    else
    {
        return 0;
    }

    for (i = 0; i < length; i++) append_byte(out, out_size, written, bytes[i]);
    return 1;
}

int json_lite_parse_string(JsonLiteCursor* cursor, char* out, size_t out_size, size_t* out_len)
{
    size_t written = 0;

    if (cursor->p >= cursor->end || *cursor->p != '"') return 0;
    cursor->p++;

    while (cursor->p < cursor->end)
    {
        unsigned char ch = (unsigned char)*cursor->p++;

        if (ch == '"')
        {
            if (out != NULL && out_size > 0)
                out[written < out_size ? written : out_size - 1] = '\0';
            if (out_len != NULL) *out_len = written;
            return 1;
        }
        if (ch < 0x20u) return 0;
        if (ch != '\\')
        {
            append_byte(out, out_size, &written, ch);
            continue;
        }

        if (cursor->p >= cursor->end) return 0;
        ch = (unsigned char)*cursor->p++;
        switch (ch)
        {
        case '"':
        case '\\':
        case '/':
            append_byte(out, out_size, &written, ch);
            break;
        case 'b':
            append_byte(out, out_size, &written, '\b');
            break;
        case 'f':
            append_byte(out, out_size, &written, '\f');
            break;
        case 'n':
            append_byte(out, out_size, &written, '\n');
            break;
        case 'r':
            append_byte(out, out_size, &written, '\r');
            break;
        case 't':
            append_byte(out, out_size, &written, '\t');
            break;
        case 'u':
        {
            unsigned codepoint;
            if (!parse_hex4(cursor, &codepoint)) return 0;

            if (codepoint >= 0xD800u && codepoint <= 0xDBFFu)
            {
                unsigned low;
                if ((size_t)(cursor->end - cursor->p) < 2 || cursor->p[0] != '\\'
                    || cursor->p[1] != 'u')
                    return 0;
                cursor->p += 2;
                if (!parse_hex4(cursor, &low)) return 0;
                if (low < 0xDC00u || low > 0xDFFFu) return 0;
                codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
            }
            else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu)
            {
                return 0;
            }

            if (codepoint == 0) return 0;
            if (!append_utf8(out, out_size, &written, codepoint)) return 0;
            break;
        }
        default:
            return 0;
        }
    }
    return 0;
}

static int skip_string(JsonLiteCursor* cursor)
{
    if (cursor->p >= cursor->end || *cursor->p != '"') return 0;
    cursor->p++;

    while (cursor->p < cursor->end)
    {
        unsigned char ch = (unsigned char)*cursor->p++;

        if (ch == '"') return 1;
        if (ch < 0x20u) return 0;
        if (ch != '\\') continue;

        if (cursor->p >= cursor->end) return 0;
        ch = (unsigned char)*cursor->p++;
        switch (ch)
        {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            break;
        case 'u':
        {
            unsigned ignored;
            if (!parse_hex4(cursor, &ignored)) return 0;
            break;
        }
        default:
            return 0;
        }
    }
    return 0;
}

static int skip_number(JsonLiteCursor* cursor)
{
    const char* p = cursor->p;

    if (p < cursor->end && *p == '-') p++;
    if (p >= cursor->end) return 0;

    if (*p == '0')
    {
        p++;
    }
    else if (*p >= '1' && *p <= '9')
    {
        do { p++; } while (p < cursor->end && *p >= '0' && *p <= '9');
    }
    else
    {
        return 0;
    }

    if (p < cursor->end && *p == '.')
    {
        p++;
        if (p >= cursor->end || *p < '0' || *p > '9') return 0;
        do { p++; } while (p < cursor->end && *p >= '0' && *p <= '9');
    }

    if (p < cursor->end && (*p == 'e' || *p == 'E'))
    {
        p++;
        if (p < cursor->end && (*p == '+' || *p == '-')) p++;
        if (p >= cursor->end || *p < '0' || *p > '9') return 0;
        do { p++; } while (p < cursor->end && *p >= '0' && *p <= '9');
    }

    cursor->p = p;
    return 1;
}

static int match_literal(JsonLiteCursor* cursor, const char* literal)
{
    size_t length = strlen(literal);
    if ((size_t)(cursor->end - cursor->p) < length) return 0;
    if (memcmp(cursor->p, literal, length) != 0) return 0;
    cursor->p += length;
    return 1;
}

static int skip_object(JsonLiteCursor* cursor, int depth)
{
    cursor->p++;
    json_lite_skip_ws(cursor);
    if (cursor->p < cursor->end && *cursor->p == '}')
    {
        cursor->p++;
        return 1;
    }

    for (;;)
    {
        if (!skip_string(cursor)) return 0;
        json_lite_skip_ws(cursor);
        if (cursor->p >= cursor->end || *cursor->p != ':') return 0;
        cursor->p++;
        /* Whitespace before a member value is part of the format, not of the
         * value: a caller may hand this scanner a pretty-printed document, and
         * json_lite_skip_value() expects the caller to have consumed it. */
        json_lite_skip_ws(cursor);
        if (!json_lite_skip_value(cursor, depth + 1)) return 0;
        json_lite_skip_ws(cursor);
        if (cursor->p >= cursor->end) return 0;
        if (*cursor->p == '}')
        {
            cursor->p++;
            return 1;
        }
        if (*cursor->p != ',') return 0;
        cursor->p++;
        json_lite_skip_ws(cursor);
    }
}

static int skip_array(JsonLiteCursor* cursor, int depth)
{
    cursor->p++;
    json_lite_skip_ws(cursor);
    if (cursor->p < cursor->end && *cursor->p == ']')
    {
        cursor->p++;
        return 1;
    }

    for (;;)
    {
        /* See skip_object(): the whitespace before an element is the format's. */
        json_lite_skip_ws(cursor);
        if (!json_lite_skip_value(cursor, depth + 1)) return 0;
        json_lite_skip_ws(cursor);
        if (cursor->p >= cursor->end) return 0;
        if (*cursor->p == ']')
        {
            cursor->p++;
            return 1;
        }
        if (*cursor->p != ',') return 0;
        cursor->p++;
        json_lite_skip_ws(cursor);
    }
}

int json_lite_skip_value(JsonLiteCursor* cursor, int depth)
{
    if (depth > JSON_LITE_MAX_DEPTH) return 0;
    if (cursor->p >= cursor->end) return 0;

    switch (*cursor->p)
    {
    case '{':
        return skip_object(cursor, depth);
    case '[':
        return skip_array(cursor, depth);
    case '"':
        return skip_string(cursor);
    case 't':
        return match_literal(cursor, "true");
    case 'f':
        return match_literal(cursor, "false");
    case 'n':
        return match_literal(cursor, "null");
    default:
        return skip_number(cursor);
    }
}

int json_lite_parse_integer(JsonLiteCursor* cursor, long* out)
{
    const char* p = cursor->p;
    int negative = 0;
    unsigned long value = 0;
    unsigned long limit;
    int digits = 0;

    if (p < cursor->end && *p == '-')
    {
        negative = 1;
        p++;
    }
    if (p >= cursor->end) return 0;

    if (*p == '0')
    {
        p++;
        digits = 1;
    }
    else if (*p >= '1' && *p <= '9')
    {
        do
        {
            unsigned digit = (unsigned)(*p - '0');
            if (value > (ULONG_MAX - digit) / 10u) return 0;
            value = value * 10u + digit;
            p++;
            digits++;
        } while (p < cursor->end && *p >= '0' && *p <= '9');
    }
    else
    {
        return 0;
    }

    if (digits == 0) return 0;
    limit = negative ? (unsigned long)LONG_MAX + 1u : (unsigned long)LONG_MAX;
    if (value > limit) return 0;

    if (out != NULL)
    {
        if (negative && value == (unsigned long)LONG_MAX + 1u)
            *out = LONG_MIN;
        else
            *out = negative ? -(long)value : (long)value;
    }

    cursor->p = p;
    return 1;
}

int json_lite_capture_value(JsonLiteCursor* cursor, const char** start, size_t* length)
{
    const char* begin;

    json_lite_skip_ws(cursor);
    begin = cursor->p;
    if (!json_lite_skip_value(cursor, 0)) return 0;
    *start = begin;
    *length = (size_t)(cursor->p - begin);
    return 1;
}

int json_lite_escape(const char* text, char* out, size_t out_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t written = 0;
    const unsigned char* p;

    if (text == NULL || out == NULL || out_size == 0) return 0;

    for (p = (const unsigned char*)text; *p != '\0'; p++)
    {
        unsigned char ch = *p;
        char temporary[7];
        size_t length;
        size_t i;

        switch (ch)
        {
        case '"':
            temporary[0] = '\\';
            temporary[1] = '"';
            length = 2;
            break;
        case '\\':
            temporary[0] = '\\';
            temporary[1] = '\\';
            length = 2;
            break;
        case '\b':
            temporary[0] = '\\';
            temporary[1] = 'b';
            length = 2;
            break;
        case '\f':
            temporary[0] = '\\';
            temporary[1] = 'f';
            length = 2;
            break;
        case '\n':
            temporary[0] = '\\';
            temporary[1] = 'n';
            length = 2;
            break;
        case '\r':
            temporary[0] = '\\';
            temporary[1] = 'r';
            length = 2;
            break;
        case '\t':
            temporary[0] = '\\';
            temporary[1] = 't';
            length = 2;
            break;
        default:
            if (ch < 0x20u)
            {
                temporary[0] = '\\';
                temporary[1] = 'u';
                temporary[2] = '0';
                temporary[3] = '0';
                temporary[4] = hex[ch >> 4];
                temporary[5] = hex[ch & 0x0Fu];
                length = 6;
            }
            else
            {
                temporary[0] = (char)ch;
                length = 1;
            }
            break;
        }

        if (written + length >= out_size) return 0;
        for (i = 0; i < length; i++) out[written++] = temporary[i];
    }

    out[written] = '\0';
    return 1;
}
