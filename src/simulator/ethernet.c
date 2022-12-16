
#include <stdint.h>
#include <stdlib.h>

#include "simulator/ethernet.h"
#include "common/utils.h"

/* Functions. */

void ethernet_initvar(struct ethernet *ether)
{
    ether->fifo_buffer = NULL;
}

void ethernet_destroy(struct ethernet *ether)
{
}

int ethernet_create(struct ethernet *ether)
{
    ethernet_initvar(ether);
    return TRUE;
}

