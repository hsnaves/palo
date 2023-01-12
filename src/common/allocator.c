
#include <stdlib.h>
#include <string.h>

#include "common/allocator.h"
#include "common/utils.h"

/* Constants. */
#define DEFAULT_SIZE                    4096

/* Functions. */
void allocator_initvar(struct allocator *a)
{
    a->buf = NULL;
    a->avail = NULL;
}

/* Deallocates each memory buffer in the linked list of buffers
 * starting at `mb`.
 */
static
void free_memory_buffers(struct memory_buffer *mb)
{
    struct memory_buffer *next;
    while (mb) {
        next = mb->next;
        free((void *) mb);
        mb = next;
    }
}

void allocator_destroy(struct allocator *a)
{
    free_memory_buffers(a->buf);
    a->buf = NULL;

    free_memory_buffers(a->avail);
    a->avail = NULL;
}

int allocator_create(struct allocator *a, size_t alignment)
{
    allocator_initvar(a);
    a->alignment = alignment;
    a->size = 0;
    a->used = 0;
    return TRUE;
}

void allocator_clear(struct allocator *a)
{
    struct memory_buffer *mb;

    mb = a->buf;
    while (mb) {
        a->buf = mb->next;
        mb->next = a->avail;
        a->avail = mb;
        mb = a->buf;
    }
    a->size = 0;
    a->used = 0;
}

void *allocator_alloc(struct allocator *a, size_t size, int zero)
{
    struct memory_buffer *mb;
    size_t alloc_size, rem;
    char *out;

    mb = a->buf;
    if (mb) {
        /* Check if there is enough space in the current buffer. */
        if (mb->used + size > mb->size) {
            mb = NULL;
        }
    }

    if (!mb) {
        /* Check if there are available buffers. */
        mb = a->avail;
        if (mb) {
            /* Check if the size is large enough. */
            if (mb->size >= size) {
                a->avail = mb->next;
            } else {
                mb = NULL;
            }
        }

        if (!mb) {
            alloc_size = size + a->alignment;
            alloc_size += sizeof(struct memory_buffer);
            if (alloc_size < DEFAULT_SIZE)
                alloc_size = DEFAULT_SIZE;

            mb = (struct memory_buffer *) malloc(alloc_size);
            if (unlikely(!mb)) {
                report_error("allocator: alloc: memory exhausted");
                return NULL;
            }

            mb->buf = (char *) &mb[1];
            if (a->alignment != 0) {
                rem = ((size_t) mb->buf) % a->alignment;
                if (rem != 0) {
                    mb->buf = &mb->buf[a->alignment - rem];
                }
            }

            mb->size = alloc_size - sizeof(struct memory_buffer);
        }

        mb->used = 0;
        mb->next = a->buf;
        a->buf = mb;
        a->size += mb->size;
    }

    out = &mb->buf[mb->used];
    if (zero) memset(out, 0, size);

    if (a->alignment != 0) {
        /* Align the size so that the next pointer is aligned. */
        rem = size % a->alignment;
        if (rem != 0) {
            size += a->alignment - rem;
            if (mb->used + size > mb->size)
                size = mb->size - mb->used;
        }
    }
    mb->used += size;
    a->used += size;
    return (void *) out;
}

char *allocator_dup(struct allocator *a, const char *s, size_t len)
{
    char *ds;

    /* To include the NUL character at the end. */
    ds = allocator_alloc(a, 1 + len, FALSE);
    if (unlikely(!ds)) return NULL;

    /* Copies the string. */
    memcpy(ds, s, len);
    ds[len] = '\0';

    return ds;
}
