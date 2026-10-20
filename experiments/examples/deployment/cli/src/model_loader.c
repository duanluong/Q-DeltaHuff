#include "model_loader.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 *  File helpers
 * ============================================================ */

static int
file_read_all (const char * path, uint8_t ** out, size_t * out_len)
{
    FILE * fp = fopen(path, "rb");
    if (fp == NULL)
    {
        printf("Cannot open %s\n", path);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        printf("Cannot seek %s\n", path);
        return -1;
    }
    long size = ftell(fp);
    rewind(fp);
    if (size < 0)
    {
        fclose(fp);
        printf("Cannot get size of %s\n", path);
        return -1;
    }

    /* +1 so text input can be NUL terminated */
    uint8_t * buf = malloc((size_t) size + 1u);
    if (buf == NULL)
    {
        fclose(fp);
        printf("Out of memory reading %s (%ld bytes)\n", path, size);
        return -1;
    }

    size_t rd = fread(buf, 1, (size_t) size, fp);
    fclose(fp);
    if (rd != (size_t) size)
    {
        free(buf);
        printf("Short read on %s\n", path);
        return -1;
    }
    buf[size] = '\0';

    *out     = buf;
    *out_len = (size_t) size;
    return 0;
}

static const char *
path_basename (const char * path)
{
    const char * base = path;
    for (const char * p = path; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }
    return base;
}

static bool
path_has_ext (const char * path, const char * ext)
{
    const char * dot = strrchr(path_basename(path), '.');
    if (dot == NULL)
    {
        return false;
    }
    for (; *dot != '\0' && *ext != '\0'; dot++, ext++)
    {
        if (tolower((unsigned char) *dot) != tolower((unsigned char) *ext))
        {
            return false;
        }
    }
    return (*dot == '\0') && (*ext == '\0');
}

static bool
is_text_model (const char * path, const uint8_t * buf, size_t len)
{
    static const char * const text_ext[] = {".h", ".hpp", ".c", ".inc", ".txt"};
    for (size_t i = 0; i < sizeof(text_ext) / sizeof(text_ext[0]); i++)
    {
        if (path_has_ext(path, text_ext[i]))
        {
            return true;
        }
    }

    /* Unknown extension: text if the first bytes are printable ASCII */
    size_t n = (len < 512u) ? len : 512u;
    for (size_t i = 0; i < n; i++)
    {
        if (!isprint(buf[i]) && !isspace(buf[i]))
        {
            return false;
        }
    }
    return (n > 0u);
}

/* ============================================================
 *  C array parser
 * ============================================================ */

typedef struct
{
    const char * begin;
    const char * p;
    const char * end;
    unsigned     line;
} scanner_t;

/* Skips whitespace, comments and (outside the array) preprocessor lines */
static void
scan_skip (scanner_t * s, bool skip_preprocessor)
{
    bool at_line_start = (s->p == s->begin) || (s->p[-1] == '\n');

    while (s->p < s->end)
    {
        char c = *s->p;
        if (c == '\n')
        {
            s->line++;
            s->p++;
            at_line_start = true;
        }
        else if (isspace((unsigned char) c))
        {
            s->p++;
        }
        else if (c == '/' && (s->p + 1) < s->end && s->p[1] == '/')
        {
            while (s->p < s->end && *s->p != '\n')
            {
                s->p++;
            }
        }
        else if (c == '/' && (s->p + 1) < s->end && s->p[1] == '*')
        {
            s->p += 2;
            while ((s->p + 1) < s->end && !(s->p[0] == '*' && s->p[1] == '/'))
            {
                if (*s->p == '\n')
                {
                    s->line++;
                }
                s->p++;
            }
            s->p = ((s->p + 1) < s->end) ? s->p + 2 : s->end;
        }
        else if (c == '#' && skip_preprocessor && at_line_start)
        {
            /* Whole directive, including backslash continuations */
            while (s->p < s->end && *s->p != '\n')
            {
                if (*s->p == '\\' && (s->p + 1) < s->end && s->p[1] == '\n')
                {
                    s->p++;
                    s->line++;
                }
                s->p++;
            }
        }
        else
        {
            return;
        }
    }
}

static bool
is_ident_char (char c)
{
    return isalnum((unsigned char) c) || c == '_';
}

static int
parse_c_array (const char * path, const char * text, size_t text_len,
               model_t * m)
{
    scanner_t s = {text, text, text + text_len, 1u};

    /* --- Find the initializer: remember the identifier before '[' --- */
    char         last_ident[sizeof(m->name)] = "";
    const char * brace                       = NULL;

    while (s.p < s.end)
    {
        scan_skip(&s, true);
        if (s.p >= s.end)
        {
            break;
        }

        if (is_ident_char(*s.p) && !isdigit((unsigned char) *s.p))
        {
            const char * start = s.p;
            while (s.p < s.end && is_ident_char(*s.p))
            {
                s.p++;
            }
            size_t n = (size_t) (s.p - start);
            if (n >= sizeof(last_ident))
            {
                n = sizeof(last_ident) - 1u;
            }
            memcpy(last_ident, start, n);
            last_ident[n] = '\0';
        }
        else if (*s.p == '[')
        {
            snprintf(m->name, sizeof(m->name), "%s", last_ident);
            s.p++;
        }
        else if (*s.p == '{')
        {
            brace = s.p;
            s.p++;
            break;
        }
        else
        {
            s.p++;
        }
    }

    if (brace == NULL)
    {
        printf("%s: no array initializer '{ ... }' found\n", path);
        return -1;
    }

    /* --- Parse the byte values (~6 characters per value in xxd output) --- */
    size_t    cap  = (text_len / 5u) + 16u;
    size_t    len  = 0;
    uint8_t * data = malloc(cap);
    if (data == NULL)
    {
        printf("%s: out of memory\n", path);
        return -1;
    }

    bool closed = false;
    while (s.p < s.end)
    {
        scan_skip(&s, false);
        if (s.p >= s.end)
        {
            break;
        }

        char c = *s.p;
        if (c == '}')
        {
            closed = true;
            s.p++;
            break;
        }
        if (c == ',')
        {
            s.p++;
            continue;
        }
        if (!isdigit((unsigned char) c))
        {
            printf("%s:%u: unexpected character '%c' in array\n",
                   path,
                   s.line,
                   c);
            free(data);
            return -1;
        }

        char *        num_end = NULL;
        unsigned long v       = strtoul(s.p, &num_end, 0);
        s.p                   = num_end;
        while (s.p < s.end && strchr("uUlL", *s.p) != NULL && *s.p != '\0')
        {
            s.p++;
        }
        if (v > 0xFFul)
        {
            printf("%s:%u: value %lu does not fit in a byte\n", path, s.line, v);
            free(data);
            return -1;
        }

        if (len == cap)
        {
            cap *= 2u;
            uint8_t * grown = realloc(data, cap);
            if (grown == NULL)
            {
                printf("%s: out of memory\n", path);
                free(data);
                return -1;
            }
            data = grown;
        }
        data[len++] = (uint8_t) v;
    }

    if (!closed)
    {
        printf("%s: array is not closed with '}'\n", path);
        free(data);
        return -1;
    }
    if (len == 0u)
    {
        printf("%s: array is empty\n", path);
        free(data);
        return -1;
    }

    /* --- Optional `<name>_len = N;` cross-check --- */
    const char * len_tag = strstr(s.p, "_len");
    if (len_tag != NULL)
    {
        const char * eq = strchr(len_tag, '=');
        if (eq != NULL)
        {
            unsigned long declared = strtoul(eq + 1, NULL, 0);
            if (declared != (unsigned long) len)
            {
                printf("Warning: %s declares _len = %lu but the array has "
                       "%zu bytes, using %zu\n",
                       path,
                       declared,
                       len,
                       len);
            }
        }
    }

    m->data = data;
    m->len  = len;
    return 0;
}

/* ============================================================
 *  Public API
 * ============================================================ */

int
model_load (const char * path, model_t * out)
{
    memset(out, 0, sizeof(*out));

    uint8_t * buf = NULL;
    size_t    len = 0;
    if (file_read_all(path, &buf, &len) != 0)
    {
        return -1;
    }

    if (is_text_model(path, buf, len))
    {
        int rc = parse_c_array(path, (const char *) buf, len, out);
        free(buf);
        if (rc != 0)
        {
            return -1;
        }
        if (out->name[0] == '\0')
        {
            snprintf(out->name, sizeof(out->name), "%s", path_basename(path));
        }
        return 0;
    }

    if (len == 0u)
    {
        printf("%s is empty\n", path);
        free(buf);
        return -1;
    }
    out->data = buf;
    out->len  = len;
    snprintf(out->name, sizeof(out->name), "%s", path_basename(path));
    return 0;
}

void
model_free (model_t * m)
{
    free(m->data);
    m->data = NULL;
    m->len  = 0;
}

int
model_write (const char * path, const uint8_t * data, size_t len)
{
    FILE * fp = fopen(path, "wb");
    if (fp == NULL)
    {
        printf("Cannot create %s\n", path);
        return -1;
    }

    int ok = 1;
    if (!path_has_ext(path, ".h"))
    {
        ok = (fwrite(data, 1, len, fp) == len);
    }
    else
    {
        /* Array / guard names from the file name, e.g. restored.h ->
         * restored / RESTORED_H */
        char         ident[128];
        char         guard[128];
        const char * base = path_basename(path);
        size_t       n    = 0;
        for (; base[n] != '\0' && base[n] != '.' && n < sizeof(ident) - 1u;
             n++)
        {
            ident[n] = is_ident_char(base[n]) ? base[n] : '_';
            guard[n] = (char) toupper((unsigned char) ident[n]);
        }
        ident[n] = '\0';
        guard[n] = '\0';
        if (n == 0u || isdigit((unsigned char) ident[0]))
        {
            snprintf(ident, sizeof(ident), "restored_model");
            snprintf(guard, sizeof(guard), "RESTORED_MODEL");
        }

        fprintf(fp, "#ifndef %s_H\n#define %s_H\n\n", guard, guard);
        fprintf(fp, "const unsigned char %s[] = {\n", ident);
        for (size_t i = 0; i < len; i++)
        {
            fprintf(fp, "0x%02X", data[i]);
            if (i != len - 1u)
            {
                fputs(", ", fp);
            }
            if ((i + 1u) % 12u == 0u || i == len - 1u)
            {
                fputc('\n', fp);
            }
        }
        fprintf(fp, "};\nconst unsigned int %s_len = %zu;\n\n#endif\n", ident, len);
        ok = !ferror(fp);
    }

    if (fclose(fp) != 0 || !ok)
    {
        printf("Failed to write %s\n", path);
        return -1;
    }
    printf("Wrote %zu bytes to %s\n", len, path);
    return 0;
}
