#ifndef __COMMON_STRING_BUFFER_H
#define __COMMON_STRING_BUFFER_H

#include <stddef.h>

/* Data structures and types. */

/* A string buffer.
 */
struct string_buffer {
    char *buffer;                 /* The character buffer. */
    size_t size;                  /* The buffer size in bytes. */
    size_t pos;                   /* Total length of the string. */
};

/* Functions */

/* Initializes the string_buffer variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void string_buffer_initvar(struct string_buffer *sb);

/* Destroys the string_buffer object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void string_buffer_destroy(struct string_buffer *sb);

/* Creates a new string_buffer object.
 * This obeys the initvar / destroy / create protocol.
 * The `size` variable specifies the buffer size.
 * Returns TRUE on success.
 */
int string_buffer_create(struct string_buffer *sb, size_t size);

/* Resets the string buffer. */
void string_buffer_reset(struct string_buffer *sb);

/* Prints a string (as in printf() function) to the buffer. */
void string_buffer_print(struct string_buffer *sb,
                         const char *fmt, ...)
    __attribute__((format (printf, 2, 3)));

/* Rewinds the string buffer by a few characters.
 * The number of characters to rewind is given in `num_chars`.
 */
void string_buffer_rewind(struct string_buffer *sb, size_t num_chars);

/* Obtains the underlying string.
 * Returns the string of the buffer.
 */
#define string_buffer_string(sb) ((sb)->buffer)

/* Obtains the length of the underlying string.
 * Returns the length.
 */
#define string_buffer_length(sb) ((sb)->pos)

#endif /* __COMMON_STRING_BUFFER_H */
