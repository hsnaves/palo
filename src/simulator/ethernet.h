
#ifndef __SIMULATOR_ETHERNET_H
#define __SIMULATOR_ETHERNET_H

#include <stdint.h>

#include "common/utils.h"

/* Data structures and types. */

/* The ethernet controller for the simulator. */
struct ethernet {
    uint16_t address;             /* The ethernet address. */
    uint16_t *fifo_buffer;        /* For sending / receiving data. */

    int32_t intr_cycle;           /* Cycle of the next interrupt. */
    uint16_t pending;             /* The task pending mask. */
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

/* Processes the F1_EMU_RSNF function.
 * Returns the serial number (address) of the ethernet card.
 */
uint16_t ethernet_rsnf(struct ethernet *ether);

/* Processes the F1_EMU_STARTF function. */
void ethernet_startf(struct ethernet *ether, uint16_t bus);

/* Performs the F1_ETH_EILFCT.
 * Returns the bus data.
 */
uint16_t ethernet_eilfct(struct ethernet *ether);

/* Performs the F1_ETH_EPFCT.
 * Returns the bus data.
 */
uint16_t ethernet_epfct(struct ethernet *ether);

/* Performs the BS_ETH_EIDFCT.
 * Returns the bus data.
 */
uint16_t ethernet_eidfct(struct ethernet *ether);

/* Performs the F1_ETH_EWFCT. */
void ethernet_ewfct(struct ethernet *ether);

/* Performs the F2_ETH_EODFCT.
 * The current value of the bus is given by `bus`.
 */
void ethernet_eodfct(struct ethernet *ether, uint16_t bus);

/* Performs the F2_ETH_EOSFCT. */
void ethernet_eosfct(struct ethernet *ether);

/* Performs the F2_ETH_ERBFCT.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t ethernet_erbfct(struct ethernet *ether);

/* Performs the F2_ETH_EEFCT. */
void ethernet_eefct(struct ethernet *ether);

/* Performs the F2_ETH_EBFCT.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t ethernet_ebfct(struct ethernet *ether);

/* Performs the F2_ETH_ECBFCT.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t ethernet_ecbfct(struct ethernet *ether);

/* Performs the F2_ETH_EISFCT. */
void ethernet_eisfct(struct ethernet *ether);

/* Processes a BLOCK instruction.
 * The task to be blocked is in the parameter `task`.
 */
void ethernet_block_task(struct ethernet *ether, uint8_t task);

/* Processes the ethernet interrupts. */
void ethernet_interrupt(struct ethernet *ether);

/* Prints the state of the registers.
 * The output is written to `output`.
 */
void ethernet_print_registers(struct ethernet *ether,
                              struct string_buffer *output);


#endif /* __SIMULATOR_ETHERNET_H */
