
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "common/string_buffer.h"
#include "common/utils.h"

/* Functions. */

void string_buffer_initvar(struct string_buffer *sb)
{
    sb->buffer = NULL;
}

void string_buffer_destroy(struct string_buffer *sb)
{
    if (sb->buffer) free((void *) sb->buffer);
    sb->buffer = NULL;
}

int string_buffer_create(struct string_buffer *sb, size_t size)
{
    string_buffer_initvar(sb);

    if (unlikely(size == 0)) {
        report_error("string_buffer: create: "
                     "invalid size");
        string_buffer_destroy(sb);
        return FALSE;
    }

    sb->buffer = (char *) malloc(size);
    if (unlikely(!sb->buffer)) {
        report_error("string_buffer: create: "
                     "memory exhausted");
        string_buffer_destroy(sb);
        return FALSE;
    }

    sb->size = size;
    sb->pos = 0;

    return TRUE;
}

void string_buffer_clear(struct string_buffer *sb)
{
    sb->pos = 0;
}

void string_buffer_print(struct string_buffer *sb,
                         const char *fmt, ...)
{
    int len;
    size_t pos;
    va_list ap;

    va_start(ap, fmt);
    pos = sb->pos;
    if (pos >= sb->size) pos = sb->size - 1;
    len = vsnprintf(&sb->buffer[pos],
                    sb->size - pos, fmt, ap);
    va_end(ap);

    if (len <= 0) return;
    sb->pos += (size_t) len;
}

void string_buffer_rewind(struct string_buffer *sb, size_t num_chars)
{
    size_t pos;

    if (sb->pos >= num_chars) {
        sb->pos -= num_chars;
    } else {
        sb->pos = 0;
    }

    pos = sb->pos;
    if (pos >= sb->size) pos = sb->size - 1;
    sb->buffer[pos] = '\0';
}
