
#ifndef __COMMON_ALLOCATOR_H
#define __COMMON_ALLOCATOR_H

#include <stddef.h>

/* Constants. */
#define DEFAULT_ALIGNMENT 16

/* Data structures and types. */

/* A memory allocator structure. This is used to allocate objects and
 * make copies of strings in the source files processed by the parser.
 */
struct allocator {
    struct memory_buffer *buf;    /* Pointer to the first buffer. */
    size_t alignment;             /* The alignment of the allocator. */
    size_t size;                  /* Total allocated size. */
    size_t used;                  /* Number of used bytes. */
};

/* A memory buffer structure. */
struct memory_buffer {
    char *buf;                    /* The actual buffer. */
    size_t size;                  /* The size of this buffer. */
    size_t used;                  /* Number of used bytes. */
    struct memory_buffer *next;   /* Pointer to the next buffer. */
};

/* Functions. */

/* Initializes the allocator variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void allocator_initvar(struct allocator *a);

/* Destroys the allocator object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void allocator_destroy(struct allocator *a);

/* Creates a new allocator object.
 * The `aligment` parameter specifies the aligment of
 * the allocated objects.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int allocator_create(struct allocator *a, size_t alignment);

/* Allocates memory from an allocator.
 * The `size` specifies the allocation size, and `zero` tells the
 * function to clear the contents of the memory.
 * Returns the pointer to the allocated memory.
 */
void *allocator_alloc(struct allocator *a, size_t size, int zero);

/* Duplicates a string.
 * The length of the string `s` is given by `len`.
 * This function includes the extra NUL byte at the end of the string
 * for convenience.
 * Returns a pointer to the duplicated string.
 */
char *allocator_dup(struct allocator *a, const char *s, size_t len);

#endif /* __COMMON_ALLOCATOR_H */
