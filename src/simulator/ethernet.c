
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
    if (ether->fifo_buffer) free((void *) ether->fifo_buffer);
    ether->fifo_buffer = NULL;
}

int ethernet_create(struct ethernet *ether)
{
    ethernet_initvar(ether);

    ethernet_reset(ether);
    return TRUE;
}

void ethernet_reset(struct ethernet *ether)
{
    ether->intr_cycle = 0xFFFFFFFFU;
    ether->eth_pending = FALSE;
}

void ethernet_startf(struct ethernet *ether, uint16_t bus)
{
}

void ethernet_interrupt(struct ethernet *ether)
{
}
