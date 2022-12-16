
#include <stdint.h>

#include "simulator/mouse.h"
#include "common/utils.h"

/* Functions. */

void mouse_initvar(struct mouse *mous)
{
}

void mouse_destroy(struct mouse *mous)
{
}

int mouse_create(struct mouse *mous)
{
    mouse_initvar(mous);
    return TRUE;
}

uint16_t mouse_poll_bits(struct mouse *mous)
{
    return 0;
}
