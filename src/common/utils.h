#ifndef __COMMON_UTILS_H
#define __COMMON_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Make sure these constants are defined. */
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* Useful macros such as MIN and MAX. */
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* (Magic) Macro to test conditions in compile time.
 * Copied from the Linux kernel.
 */
#define BUILD_BUG_ON(condition) \
    ((void)sizeof(char[1 - 2*!!(condition)]))

/* Macros to help branch prediction. */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Other useful macros. */
#define __inline__ __inline__
#define __aligned__(x) __attribute__((aligned (x)))
#define __restrict__ __restrict__

/* Data structures and types. */

/* A string buffer. */
struct string_buffer {
    char *buf;                    /* The character buffer. */
    size_t buf_size;              /* The buffer size in bytes. */
    size_t len;                   /* Total length of the string. */
};

/* Functions */

/* Reports an error to stderr.
 * This function follows the same parameter convetion as printf().
 */
void report_error(const char *fmt, ...)
    __attribute__((format (printf, 1, 2)));

/* Resets the string buffer. */
void string_buffer_reset(struct string_buffer *buf);

/* Prints a string (as in printf() function) to the buffer. */
void string_buffer_print(struct string_buffer *buf,
                         const char *fmt, ...)
    __attribute__((format (printf, 2, 3)));

/* Rewinds the string buffer by a few characters.
 * The number of characters to rewind is given in `num_chars`.
 */
void string_buffer_rewind(struct string_buffer *buf, size_t num_chars);

#endif /* __COMMON_UTILS_H */
