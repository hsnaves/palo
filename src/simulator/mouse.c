
#include <stdint.h>

#include "simulator/mouse.h"
#include "common/utils.h"

/* Functions. */

void mouse_initvar(struct mouse *m)
{
}

void mouse_destroy(struct mouse *m)
{
}

int mouse_create(struct mouse *m)
{
    mouse_initvar(m);
    return TRUE;
}

uint16_t mouse_poll_bits(struct mouse *m)
{
    return 0;
}
