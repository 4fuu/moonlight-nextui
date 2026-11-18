#include "pak_manifest.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PAK_MANIFEST_MAX_BYTES (64u * 1024u)
#define JSON_MAX_DEPTH 32
#define JSON_KEY_MAX 64
#define PAK_VERSION_CONTENT_MAX (PAK_MANIFEST_VERSION_MAX + 2)

typedef struct
{
    const char* p;
    const char* end;
} JsonCursor;

typedef struct
{
    unsigned major;
    unsigned minor;
    unsigned patch;
} SemVer;

/* ------------------------------------------------------------------------- */
/* Semver                                                                     */
/* ------------------------------------------------------------------------- */

static int parse_semver(const char* text, int require_v_prefix, SemVer* out)
{
    const unsigned char* p;
    unsigned parts[3];
    int i;

    if (text == NULL || text[0] == '\0') return 0;

    p = (const unsigned char*)text;
    if (*p == 'v')
    {
        p++;
    }
    else if (require_v_prefix)
    {
        return 0;
    }

    for (i = 0; i < 3; i++)
    {
        unsigned value = 0;
        int digits = 0;

        while (*p >= '0' && *p <= '9')
        {
            if (value > 100000000u) return 0;
            value = value * 10u + (unsigned)(*p - '0');
            p++;
            digits++;
        }
        if (digits == 0) return 0;
        parts[i] = value;

        if (i < 2)
        {
            if (*p != '.') return 0;
            p++;
        }
    }
    if (*p != '\0') return 0;

    if (out != NULL)
    {
        out->major = parts[0];
        out->minor = parts[1];
        out->patch = parts[2];
    }
    return 1;
}

static int semver_compare(const SemVer* a, const SemVer* b)
{
    if (a->major != b->major) return a->major < b->major ? -1 : 1;
    if (a->minor != b->minor) return a->minor < b->minor ? -1 : 1;
    if (a->patch != b->patch) return a->patch < b->patch ? -1 : 1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Minimal JSON scanner                                                       */
/*                                                                             */
/* Only the top-level object is materialized; nested objects/arrays are        */
/* validated and skipped. This is deliberately small and has no dependency on  */
/* a JSON library, following the hand-written ra_util.h precedent.             */
/* ------------------------------------------------------------------------- */

static void json_skip_ws(JsonCursor* c)
{
    while (c->p < c->end)
    {
        char ch = *c->p;
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
        c->p++;
    }
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int json_parse_hex4(JsonCursor* c, unsigned* out)
{
    unsigned value = 0;
    int i;

    if ((size_t)(c->end - c->p) < 4) return 0;
    for (i = 0; i < 4; i++)
    {
        int digit = hex_value(c->p[i]);
        if (digit < 0) return 0;
        value = (value << 4) | (unsigned)digit;
    }
    c->p += 4;
    *out = value;
    return 1;
}

static void append_byte(char* out, size_t out_size, size_t* written, unsigned char ch)
{
    if (out != NULL && out_size > 0 && *written < out_size - 1)
        out[*written] = (char)ch;
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

/*
 * Parse one JSON string. When out is NULL the string is validated and skipped.
 * out_len receives the full decoded length, even when the destination buffer
 * was too small, so callers can treat an overlong value as missing.
 */
static int json_parse_string(JsonCursor* c, char* out, size_t out_size, size_t* out_len)
{
    size_t written = 0;

    if (c->p >= c->end || *c->p != '"') return 0;
    c->p++;

    while (c->p < c->end)
    {
        unsigned char ch = (unsigned char)*c->p++;

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

        if (c->p >= c->end) return 0;
        ch = (unsigned char)*c->p++;
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
            if (!json_parse_hex4(c, &codepoint)) return 0;

            if (codepoint >= 0xD800u && codepoint <= 0xDBFFu)
            {
                unsigned low;
                if ((size_t)(c->end - c->p) < 2 || c->p[0] != '\\' || c->p[1] != 'u') return 0;
                c->p += 2;
                if (!json_parse_hex4(c, &low)) return 0;
                if (low < 0xDC00u || low > 0xDFFFu) return 0;
                codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
            }
            else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu)
            {
                return 0;
            }

            /* Metadata is consumed through C strings; an embedded NUL would
             * silently truncate it, so treat that manifest as malformed. */
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

static int json_skip_number(JsonCursor* c)
{
    const char* p = c->p;

    if (p < c->end && *p == '-') p++;
    if (p >= c->end) return 0;

    if (*p == '0')
    {
        p++;
    }
    else if (*p >= '1' && *p <= '9')
    {
        do { p++; } while (p < c->end && *p >= '0' && *p <= '9');
    }
    else
    {
        return 0;
    }

    if (p < c->end && *p == '.')
    {
        p++;
        if (p >= c->end || *p < '0' || *p > '9') return 0;
        do { p++; } while (p < c->end && *p >= '0' && *p <= '9');
    }

    if (p < c->end && (*p == 'e' || *p == 'E'))
    {
        p++;
        if (p < c->end && (*p == '+' || *p == '-')) p++;
        if (p >= c->end || *p < '0' || *p > '9') return 0;
        do { p++; } while (p < c->end && *p >= '0' && *p <= '9');
    }

    c->p = p;
    return 1;
}

static int json_match_literal(JsonCursor* c, const char* literal)
{
    size_t length = strlen(literal);
    if ((size_t)(c->end - c->p) < length) return 0;
    if (memcmp(c->p, literal, length) != 0) return 0;
    c->p += length;
    return 1;
}

/*
 * Lexically skip a string without decoding it. Unknown fields are validated as
 * JSON but their Unicode escapes are left alone, so metadata this build does
 * not consume cannot make an otherwise valid manifest fail.
 */
static int json_skip_string(JsonCursor* c)
{
    if (c->p >= c->end || *c->p != '"') return 0;
    c->p++;

    while (c->p < c->end)
    {
        unsigned char ch = (unsigned char)*c->p++;

        if (ch == '"') return 1;
        if (ch < 0x20u) return 0;
        if (ch != '\\') continue;

        if (c->p >= c->end) return 0;
        ch = (unsigned char)*c->p++;
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
            if (!json_parse_hex4(c, &ignored)) return 0;
            break;
        }
        default:
            return 0;
        }
    }
    return 0;
}

static int json_skip_value(JsonCursor* c, int depth);

static int json_skip_object(JsonCursor* c, int depth)
{
    c->p++; /* '{' */
    json_skip_ws(c);
    if (c->p < c->end && *c->p == '}')
    {
        c->p++;
        return 1;
    }

    for (;;)
    {
        if (!json_skip_string(c)) return 0;
        json_skip_ws(c);
        if (c->p >= c->end || *c->p != ':') return 0;
        c->p++;
        if (!json_skip_value(c, depth + 1)) return 0;
        json_skip_ws(c);
        if (c->p >= c->end) return 0;
        if (*c->p == '}')
        {
            c->p++;
            return 1;
        }
        if (*c->p != ',') return 0;
        c->p++;
        json_skip_ws(c);
    }
}

static int json_skip_array(JsonCursor* c, int depth)
{
    c->p++; /* '[' */
    json_skip_ws(c);
    if (c->p < c->end && *c->p == ']')
    {
        c->p++;
        return 1;
    }

    for (;;)
    {
        if (!json_skip_value(c, depth + 1)) return 0;
        json_skip_ws(c);
        if (c->p >= c->end) return 0;
        if (*c->p == ']')
        {
            c->p++;
            return 1;
        }
        if (*c->p != ',') return 0;
        c->p++;
        json_skip_ws(c);
    }
}

static int json_skip_value(JsonCursor* c, int depth)
{
    if (depth > JSON_MAX_DEPTH) return 0;

    json_skip_ws(c);
    if (c->p >= c->end) return 0;

    switch (*c->p)
    {
    case '"':
        return json_skip_string(c);
    case '{':
        return json_skip_object(c, depth);
    case '[':
        return json_skip_array(c, depth);
    case 't':
        return json_match_literal(c, "true");
    case 'f':
        return json_match_literal(c, "false");
    case 'n':
        return json_match_literal(c, "null");
    default:
        return json_skip_number(c);
    }
}

/* ------------------------------------------------------------------------- */
/* Manifest fields                                                            */
/* ------------------------------------------------------------------------- */

static unsigned capability_for_name(const char* name)
{
    if (strcmp(name, "saves") == 0) return PAK_CAPABILITY_SAVES;
    if (strcmp(name, "overlay") == 0) return PAK_CAPABILITY_OVERLAY;
    return 0;
}

static int parse_capabilities(JsonCursor* c, PakManifest* manifest)
{
    if (c->p >= c->end || *c->p != '[') return 0;
    c->p++;

    manifest->capabilities_declared = 1;
    json_skip_ws(c);
    if (c->p < c->end && *c->p == ']')
    {
        c->p++;
        return 1;
    }

    for (;;)
    {
        if (c->p >= c->end) return 0;

        if (*c->p == '"')
        {
            char name[32];
            size_t length = 0;
            if (!json_parse_string(c, name, sizeof name, &length)) return 0;
            if (length < sizeof name) manifest->capabilities |= capability_for_name(name);
        }
        else
        {
            /* A capability this build cannot represent is ignored, not fatal. */
            if (!json_skip_value(c, 2)) return 0;
        }

        json_skip_ws(c);
        if (c->p >= c->end) return 0;
        if (*c->p == ']')
        {
            c->p++;
            return 1;
        }
        if (*c->p != ',') return 0;
        c->p++;
        json_skip_ws(c);
    }
}

static int add_device_capability(PakManifest* manifest, const char* name)
{
    int i;

    if (name == NULL || name[0] == '\0') return 1;

    for (i = 0; i < manifest->device_capability_count; i++)
    {
        if (strcmp(manifest->device_capabilities[i], name) == 0) return 1;
    }
    if (manifest->device_capability_count >= PAK_MANIFEST_DEVICE_CAP_MAX) return 1;

    strcpy(manifest->device_capabilities[manifest->device_capability_count], name);
    manifest->device_capability_count++;
    return 1;
}

static int parse_device_capabilities(JsonCursor* c, PakManifest* manifest)
{
    if (c->p >= c->end || *c->p != '[') return 0;
    c->p++;

    json_skip_ws(c);
    if (c->p < c->end && *c->p == ']')
    {
        c->p++;
        return 1;
    }

    for (;;)
    {
        if (c->p >= c->end) return 0;

        if (*c->p == '"')
        {
            char name[PAK_MANIFEST_DEVICE_CAP_NAME_MAX];
            size_t length = 0;
            if (!json_parse_string(c, name, sizeof name, &length)) return 0;
            if (length < sizeof name) (void)add_device_capability(manifest, name);
        }
        else
        {
            /* A non-string request is ignored, not fatal: it can never grant. */
            if (!json_skip_value(c, 2)) return 0;
        }

        json_skip_ws(c);
        if (c->p >= c->end) return 0;
        if (*c->p == ']')
        {
            c->p++;
            return 1;
        }
        if (*c->p != ',') return 0;
        c->p++;
        json_skip_ws(c);
    }
}

/*
 * Parse the optional device object. Only "capabilities" is consumed here;
 * other keys (LED profile requests, restore preferences, ...) belong to later
 * tasks and are validated but skipped.
 */
static int parse_device_section(JsonCursor* c, PakManifest* manifest)
{
    int seen_capabilities = 0;

    if (c->p >= c->end || *c->p != '{') return 0;
    c->p++;
    manifest->device_declared = 1;

    json_skip_ws(c);
    if (c->p < c->end && *c->p == '}')
    {
        c->p++;
        return 1;
    }

    for (;;)
    {
        char key[JSON_KEY_MAX];
        size_t key_length = 0;

        if (!json_parse_string(c, key, sizeof key, &key_length)) return 0;
        json_skip_ws(c);
        if (c->p >= c->end || *c->p != ':') return 0;
        c->p++;
        json_skip_ws(c);

        if (key_length < sizeof key && strcmp(key, "capabilities") == 0)
        {
            if (!seen_capabilities)
            {
                seen_capabilities = 1;
                if (c->p < c->end && *c->p == '[')
                {
                    if (!parse_device_capabilities(c, manifest)) return 0;
                }
                else if (!json_skip_value(c, 2))
                {
                    return 0;
                }
            }
            else if (!json_skip_value(c, 2))
            {
                return 0;
            }
        }
        else if (!json_skip_value(c, 2))
        {
            return 0;
        }

        json_skip_ws(c);
        if (c->p >= c->end) return 0;
        if (*c->p == '}')
        {
            c->p++;
            return 1;
        }
        if (*c->p != ',') return 0;
        c->p++;
        json_skip_ws(c);
    }
}

/* Parse a wanted string field, or validate and skip a wrong-typed value. */
static int parse_string_value(JsonCursor* c, char* out, size_t out_size, size_t* out_len)
{
    *out_len = 0;
    if (c->p >= c->end) return 0;
    if (*c->p != '"') return json_skip_value(c, 1);
    return json_parse_string(c, out, out_size, out_len);
}

static void drop_overlong(char* out, size_t out_size, size_t length)
{
    if (length >= out_size) out[0] = '\0';
}

enum
{
    SEEN_ID = 1u << 0,
    SEEN_NAME = 1u << 1,
    SEEN_VERSION = 1u << 2,
    SEEN_EXEC = 1u << 3,
    SEEN_ICON = 1u << 4,
    SEEN_MIN_SYSTEM = 1u << 5,
    SEEN_APOSTROPHE = 1u << 6,
    SEEN_CAPABILITIES = 1u << 7,
    SEEN_DEVICE = 1u << 8,
};

static int parse_manifest(const char* json, size_t length, PakManifest* manifest)
{
    JsonCursor c;
    unsigned seen = 0;

    c.p = json;
    c.end = json + length;

    /* Accept a UTF-8 BOM: some editors write one even though JSON omits it. */
    if (length >= 3 && (unsigned char)c.p[0] == 0xEF && (unsigned char)c.p[1] == 0xBB
        && (unsigned char)c.p[2] == 0xBF)
        c.p += 3;

    json_skip_ws(&c);
    if (c.p >= c.end || *c.p != '{') return 0;
    c.p++;
    json_skip_ws(&c);

    if (c.p < c.end && *c.p == '}')
    {
        c.p++;
    }
    else
    {
        for (;;)
        {
            char key[JSON_KEY_MAX];
            size_t key_length = 0;
            int known;
            size_t value_length;

            if (!json_parse_string(&c, key, sizeof key, &key_length)) return 0;
            json_skip_ws(&c);
            if (c.p >= c.end || *c.p != ':') return 0;
            c.p++;
            json_skip_ws(&c);

            known = key_length < sizeof key;
            if (known && strcmp(key, "id") == 0)
            {
                if (!(seen & SEEN_ID))
                {
                    seen |= SEEN_ID;
                    if (!parse_string_value(&c, manifest->id, sizeof manifest->id, &value_length)) return 0;
                    drop_overlong(manifest->id, sizeof manifest->id, value_length);
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "name") == 0)
            {
                if (!(seen & SEEN_NAME))
                {
                    seen |= SEEN_NAME;
                    if (!parse_string_value(&c, manifest->name, sizeof manifest->name, &value_length)) return 0;
                    drop_overlong(manifest->name, sizeof manifest->name, value_length);
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "version") == 0)
            {
                if (!(seen & SEEN_VERSION))
                {
                    seen |= SEEN_VERSION;
                    if (!parse_string_value(&c, manifest->version, sizeof manifest->version, &value_length)) return 0;
                    drop_overlong(manifest->version, sizeof manifest->version, value_length);
                    manifest->version_valid = parse_semver(manifest->version, 1, NULL);
                    if (!manifest->version_valid) manifest->version[0] = '\0';
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "exec") == 0)
            {
                if (!(seen & SEEN_EXEC))
                {
                    seen |= SEEN_EXEC;
                    if (!parse_string_value(&c, manifest->exec, sizeof manifest->exec, &value_length)) return 0;
                    drop_overlong(manifest->exec, sizeof manifest->exec, value_length);
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "icon") == 0)
            {
                if (!(seen & SEEN_ICON))
                {
                    seen |= SEEN_ICON;
                    if (!parse_string_value(&c, manifest->icon, sizeof manifest->icon, &value_length)) return 0;
                    drop_overlong(manifest->icon, sizeof manifest->icon, value_length);
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "min_system") == 0)
            {
                if (!(seen & SEEN_MIN_SYSTEM))
                {
                    seen |= SEEN_MIN_SYSTEM;
                    if (!parse_string_value(&c, manifest->min_system, sizeof manifest->min_system, &value_length)) return 0;
                    drop_overlong(manifest->min_system, sizeof manifest->min_system, value_length);
                    manifest->min_system_valid = parse_semver(manifest->min_system, 0, NULL);
                    if (!manifest->min_system_valid) manifest->min_system[0] = '\0';
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "apostrophe") == 0)
            {
                if (!(seen & SEEN_APOSTROPHE))
                {
                    seen |= SEEN_APOSTROPHE;
                    if (!parse_string_value(&c, manifest->apostrophe, sizeof manifest->apostrophe, &value_length)) return 0;
                    drop_overlong(manifest->apostrophe, sizeof manifest->apostrophe, value_length);
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "capabilities") == 0)
            {
                if (!(seen & SEEN_CAPABILITIES))
                {
                    seen |= SEEN_CAPABILITIES;
                    if (c.p < c.end && *c.p == '[')
                    {
                        if (!parse_capabilities(&c, manifest)) return 0;
                    }
                    else if (!json_skip_value(&c, 1))
                    {
                        return 0;
                    }
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (known && strcmp(key, "device") == 0)
            {
                if (!(seen & SEEN_DEVICE))
                {
                    seen |= SEEN_DEVICE;
                    if (c.p < c.end && *c.p == '{')
                    {
                        if (!parse_device_section(&c, manifest)) return 0;
                    }
                    else if (!json_skip_value(&c, 1))
                    {
                        return 0;
                    }
                }
                else if (!json_skip_value(&c, 1)) return 0;
            }
            else if (!json_skip_value(&c, 1))
            {
                return 0;
            }

            json_skip_ws(&c);
            if (c.p >= c.end) return 0;
            if (*c.p == '}')
            {
                c.p++;
                break;
            }
            if (*c.p != ',') return 0;
            c.p++;
            json_skip_ws(&c);
        }
    }

    json_skip_ws(&c);
    return c.p == c.end;
}

/* ------------------------------------------------------------------------- */
/* Paths and version record                                                   */
/* ------------------------------------------------------------------------- */

int pak_manifest_is_safe_id(const char* id)
{
    const unsigned char* p;

    if (id == NULL || id[0] == '\0') return 0;
    if (strcmp(id, ".") == 0 || strcmp(id, "..") == 0) return 0;
    if (strstr(id, "..") != NULL) return 0;

    for (p = (const unsigned char*)id; *p != '\0'; p++)
    {
        if (*p < 0x20u || *p == 0x7Fu) return 0;
        if (*p == '/' || *p == '\\') return 0;
    }
    return 1;
}

static int path_basename(const char* path, char* out, size_t out_size)
{
    size_t end;
    size_t start;
    size_t length;

    if (path == NULL || out == NULL || out_size == 0) return 0;

    end = strlen(path);
    while (end > 0 && path[end - 1] == '/') end--;
    if (end == 0) return 0;

    start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    length = end - start;
    if (length == 0 || length >= out_size) return 0;

    memcpy(out, path + start, length);
    out[length] = '\0';
    return 1;
}

static int ensure_dir(const char* path)
{
    char work[PAK_MANIFEST_PATH_MAX];
    size_t length;
    char* p;

    if (path == NULL || path[0] == '\0') return 0;
    length = strlen(path);
    if (length >= sizeof work) return 0;
    memcpy(work, path, length + 1);

    while (length > 1 && work[length - 1] == '/') work[--length] = '\0';

    for (p = work + 1; *p != '\0'; p++)
    {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(work, 0755) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }

    if (mkdir(work, 0755) != 0)
    {
        struct stat st;
        if (errno != EEXIST) return 0;
        if (stat(work, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    }
    return 1;
}

static int file_has_content(const char* path, const char* content)
{
    char buffer[PAK_VERSION_CONTENT_MAX];
    size_t expected = strlen(content);
    size_t length;
    FILE* file;

    file = fopen(path, "rb");
    if (file == NULL) return 0;
    length = fread(buffer, 1, sizeof buffer, file);
    if (ferror(file) || length != expected || memcmp(buffer, content, length) != 0)
    {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static int record_installed_version(const char* version_dir, const char* id, const char* version)
{
    char path[PAK_MANIFEST_PATH_MAX];
    char temporary[PAK_MANIFEST_PATH_MAX];
    char content[PAK_VERSION_CONTENT_MAX];
    size_t length;
    int written;
    FILE* file;
    int ok;

    if (version_dir == NULL || version_dir[0] == '\0') return 0;
    if (!pak_manifest_is_safe_id(id) || version == NULL || version[0] == '\0') return 0;
    if (!ensure_dir(version_dir)) return 0;

    written = snprintf(path, sizeof path, "%s/%s.version", version_dir, id);
    if (written < 0 || (size_t)written >= sizeof path) return 0;

    written = snprintf(content, sizeof content, "%s\n", version);
    if (written < 0 || (size_t)written >= sizeof content) return 0;

    if (file_has_content(path, content)) return 1;

    written = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof temporary) return 0;

    file = fopen(temporary, "wb");
    if (file == NULL) return 0;

    length = strlen(content);
    ok = fwrite(content, 1, length, file) == length;
    if (fclose(file) != 0) ok = 0;
    if (!ok)
    {
        (void)remove(temporary);
        return 0;
    }
    if (rename(temporary, path) != 0)
    {
        (void)remove(temporary);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

PakManifestStatus pak_manifest_load(const char* pak_dir, PakManifest* manifest)
{
    char path[PAK_MANIFEST_PATH_MAX + 16];
    char* buffer;
    size_t length;
    int read_error;
    int too_long;
    int written;
    struct stat st;
    FILE* file;

    if (manifest == NULL) return PAK_MANIFEST_INVALID;
    memset(manifest, 0, sizeof *manifest);
    manifest->status = PAK_MANIFEST_ABSENT;

    if (pak_dir == NULL || pak_dir[0] == '\0')
    {
        manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }

    written = snprintf(path, sizeof path, "%s/pak.json", pak_dir);
    if (written < 0 || (size_t)written >= sizeof path)
    {
        manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }

    if (stat(path, &st) != 0)
    {
        if (errno != ENOENT) manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }
    if (!S_ISREG(st.st_mode) || st.st_size > (off_t)PAK_MANIFEST_MAX_BYTES)
    {
        manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }

    file = fopen(path, "rb");
    if (file == NULL)
    {
        if (errno != ENOENT) manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }

    buffer = malloc(PAK_MANIFEST_MAX_BYTES + 1);
    if (buffer == NULL)
    {
        fclose(file);
        manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }

    length = fread(buffer, 1, PAK_MANIFEST_MAX_BYTES, file);
    read_error = ferror(file) != 0;
    too_long = !feof(file);
    fclose(file);

    if (read_error || too_long)
    {
        free(buffer);
        manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }
    buffer[length] = '\0';

    if (!parse_manifest(buffer, length, manifest))
    {
        free(buffer);
        memset(manifest, 0, sizeof *manifest);
        manifest->status = PAK_MANIFEST_INVALID;
        return manifest->status;
    }
    free(buffer);

    manifest->status = PAK_MANIFEST_OK;

    if (!pak_manifest_is_safe_id(manifest->id))
    {
        char base[PAK_MANIFEST_ID_MAX];
        if (path_basename(pak_dir, base, sizeof base) && pak_manifest_is_safe_id(base))
            strcpy(manifest->id, base);
        else
            manifest->id[0] = '\0';
    }

    return manifest->status;
}

PakManifestStatus pak_manifest_note_scan(const char* pak_dir, const char* version_dir)
{
    PakManifest manifest;
    PakManifestStatus status = pak_manifest_load(pak_dir, &manifest);

    if (status == PAK_MANIFEST_OK && manifest.version_valid && manifest.id[0] != '\0')
        (void)record_installed_version(version_dir, manifest.id, manifest.version);

    return status;
}

int pak_manifest_has_capability(const PakManifest* manifest, PakCapability capability)
{
    if (manifest == NULL || manifest->status != PAK_MANIFEST_OK) return 0;
    return (manifest->capabilities & (unsigned)capability) != 0;
}

int pak_manifest_requests_device_capability(const PakManifest* manifest, const char* capability)
{
    int i;

    if (manifest == NULL || manifest->status != PAK_MANIFEST_OK) return 0;
    if (capability == NULL || capability[0] == '\0') return 0;

    for (i = 0; i < manifest->device_capability_count; i++)
    {
        if (strcmp(manifest->device_capabilities[i], capability) == 0) return 1;
    }
    return 0;
}

int pak_manifest_meets_min_system(const PakManifest* manifest, const char* system_version)
{
    SemVer required;
    SemVer installed;

    if (manifest == NULL || manifest->status != PAK_MANIFEST_OK) return 1;
    if (!manifest->min_system_valid) return 1;
    if (!parse_semver(manifest->min_system, 0, &required)) return 1;
    if (system_version == NULL || !parse_semver(system_version, 0, &installed)) return 0;

    return semver_compare(&installed, &required) >= 0;
}
