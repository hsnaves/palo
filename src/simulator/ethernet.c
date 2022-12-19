
#include <stdint.h>
#include <stdlib.h>

#include "simulator/ethernet.h"
#include "microcode/microcode.h"
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
    ether->intr_cycle = -1;
    ether->pending = 0;
}

uint16_t ethernet_rsnf(struct ethernet *ether)
{
    return (0xFF00U | ether->address);
}

void ethernet_startf(struct ethernet *ether, uint16_t bus)
{
    /* TODO: Implement this. */
}

uint16_t ethernet_eilfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
    return 0x0;
}

uint16_t ethernet_epfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
    return 0x0;
}

uint16_t ethernet_eidfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
    return 0x0;
}

void ethernet_ewfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
}

void ethernet_eodfct(struct ethernet *ether, uint16_t bus)
{
    /* TODO: Implement this. */
}

void ethernet_eosfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
}

uint16_t ethernet_erbfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
    return 0x0;
}

void ethernet_eefct(struct ethernet *ether)
{
    /* TODO: Implement this. */
}

uint16_t ethernet_ebfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
    return 0x0;
}

uint16_t ethernet_ecbfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
    return 0x0;
}

void ethernet_eisfct(struct ethernet *ether)
{
    /* TODO: Implement this. */
}

void ethernet_block_task(struct ethernet *ether, uint8_t task)
{
    /* TODO: Implement this. */
    ether->pending &= ~(1 << task);
}

void ethernet_interrupt(struct ethernet *ether)
{
    /* TODO: Implement this. */
}
