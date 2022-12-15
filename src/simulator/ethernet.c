
#include <stdint.h>
#include <stdlib.h>

#include "simulator/ethernet.h"
#include "common/utils.h"

/* Functions. */

void ethernet_initvar(struct ethernet *e)
{
    e->fifo_buffer = NULL;
}

void ethernet_destroy(struct ethernet *e)
{
}

int ethernet_create(struct ethernet *e)
{
    ethernet_initvar(e);
    return TRUE;
}

