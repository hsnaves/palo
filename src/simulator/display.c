
#include <stdint.h>
#include <stdlib.h>

#include "simulator/display.h"
#include "common/utils.h"

/* Functions. */

void display_initvar(struct display *displ)
{
    displ->data_buffer = NULL;
}

void display_destroy(struct display *displ)
{
    if (displ->data_buffer) free((void *) displ->data_buffer);
    displ->data_buffer = NULL;
}

int display_create(struct display *displ)
{
    display_initvar(displ);

    return TRUE;
}

