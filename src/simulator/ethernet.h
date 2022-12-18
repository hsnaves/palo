
#ifndef __SIMULATOR_ETHERNET_H
#define __SIMULATOR_ETHERNET_H

#include <stdint.h>

/* Data structures and types. */

/* The ethernet controller for the simulator. */
struct ethernet {
    uint16_t address;             /* The ethernet address. */
    uint16_t *fifo_buffer;        /* For sending / receiving data. */

    uint32_t intr_cycle;          /* Cycle of the next interrupt. */
    int eth_pending;              /* If the ethernet task is pending. */
};

/* Functions. */

/* Initializes the ethernet variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void ethernet_initvar(struct ethernet *ether);

/* Destroys the ethernet object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void ethernet_destroy(struct ethernet *ether);

/* Creates a new ethernet object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int ethernet_create(struct ethernet *ether);

/* Resets the ethernet controller. */
void ethernet_reset(struct ethernet *ether);

/* Processes the F1_EMU_STARTF function. */
void ethernet_startf(struct ethernet *ether, uint16_t bus);

/* Processes the ethernet interrupts. */
void ethernet_interrupt(struct ethernet *ether);

#endif /* __SIMULATOR_ETHERNET_H */
