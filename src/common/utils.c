
#include <stdio.h>
#include <stdarg.h>
#include "common/utils.h"

void report_error(const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

void string_buffer_reset(struct string_buffer *buf)
{
    buf->len = 0;
}

void string_buffer_print(struct string_buffer *buf,
                         const char *fmt, ...)
{
    int len;
    size_t pos;
    va_list ap;

    va_start(ap, fmt);
    if (buf->buf && buf->buf_size > 0) {
        pos = buf->len;
        if (pos >= buf->buf_size) pos = buf->buf_size - 1;
        len = vsnprintf(&buf->buf[pos],
                        buf->buf_size - pos,
                        fmt, ap);
    } else {
        /* Pretend that one character was printed, to
         * avoid the call to vsnprintf().
         */
        len = 1;
    }
    va_end(ap);

    if (len <= 0) return;
    buf->len += (size_t) len;
}

void string_buffer_rewind(struct string_buffer *buf, size_t num_chars)
{
    size_t pos;

    if (buf->len >= num_chars) {
        buf->len -= num_chars;
    } else {
        buf->len = 0;
    }

    if (buf->buf && buf->buf_size > 0) {
        pos = buf->len;
        if (pos >= buf->buf_size) pos = buf->buf_size - 1;
        buf->buf[pos] = '\0';
    }
}
