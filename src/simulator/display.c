
#include <stdint.h>
#include <stdlib.h>

#include "simulator/display.h"
#include "common/utils.h"

/* Functions. */

void display_initvar(struct display *d)
{
    d->data_buffer = NULL;
}

void display_destroy(struct display *d)
{
    if (d->data_buffer) free((void *) d->data_buffer);
    d->data_buffer = NULL;
}

int display_create(struct display *d)
{
    display_initvar(d);

    return TRUE;
}

