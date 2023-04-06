
#ifndef __SIMULATOR_ETHERNET_H
#define __SIMULATOR_ETHERNET_H

#include <stdint.h>

#include "microcode/microcode.h"
#include "common/serdes.h"
#include "common/string_buffer.h"

/* Data structures and types. */

/* Transport object used by the ethernet device. */
struct transport {
    /* To clear the TX buffer. */
    void (*clear_tx)(void *arg);

    /* To append a word to the current TX packet to be sent.
     * The parameter `data` contains the word to be appended.
     * Returns TRUE on success.
     */
    int (*append_tx)(void *arg, uint16_t data);

    /* To send the current packet.
     * Returns TRUE on success.
     */
    int (*send)(void *arg);

    /* To enable (or disable) receiving packets.
     * The parameter `enable` specifies wheter to enable or disable
     * receiving packets.
     * Returns TRUE on success.
     */
    int (*enable_rx)(void *arg, int enable);

    /* To clear the RX buffer. */
    void (*clear_rx)(void *arg);

    /* Gets the data of the current packet.
     * Returns the current data (or zero if no data).
     */
    uint16_t (*get_rx_data)(void *arg);

    /* Checks if there is still remaining data on the current received
     * packet. Returns the number of remaining bytes.
     */
    size_t (*has_rx_data)(void *arg);

    /* Receives a packet.
     * The `len` parameter receives the length of the message.
     * Returns TRUE on success.
     */
    int (*receive)(void *arg, size_t *plen);

    void *arg;                    /* The argument to the callbacks */
};

/* The ethernet controller for the simulator. */
struct ethernet {
    struct transport *trp;        /* The transport object. */

    uint16_t address;             /* The (PUP) ethernet address. */
    uint16_t *fifo;               /* For sending / receiving data. */
    uint8_t fifo_start, fifo_end; /* To control the FIFO. */

    uint16_t iocmd;
    int out_busy;
    int in_busy;
    int in_gone;
    uint16_t input_state;

    /* NS below means not simulated. */
    int data_late;                /* Data late detected (NS). */
    int collision;                /* Packet collision detected (NS). */
    int crc_bad;                  /* Bad CRC deteced (NS). */
    int incomplete;               /* Incomplete packet (NS). */

    uint16_t status;              /* Latched status. */

    int countdown_wakeup;         /* Causes a wakeup on the next tick
                                   * of SWAKMRT.
                                   */
    int end_tx;                   /* To end the current transmission. */

    int32_t intr_cycle;           /* Cycle of the next interrupt. */
    int32_t tx_intr_cycle;        /* Cycle of the next transmission. */
    int32_t rx_intr_cycle;        /* Cycle of the next receive. */
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

/* Sets the transport object.
 * The transport object is given by `trp`.
 */
void ethernet_set_transport(struct ethernet *ether,
                            struct transport *trp);

/* Sets the ethernet address.
 * The address is given in the parameter `address`.
 */
void ethernet_set_address(struct ethernet *ether, uint16_t address);

/* Resets the ethernet controller.
 * Returns TRUE on success.
 */
int ethernet_reset(struct ethernet *ether);

/* Processes the F1_EMU_RSNF function.
 * Returns the serial number (address) of the ethernet card.
 */
uint16_t ethernet_rsnf(struct ethernet *ether);

/* Processes the F1_EMU_STARTF function. */
void ethernet_startf(struct ethernet *ether, uint16_t bus);

/* Performs the F1_ETH_EILFCT (Ethernet Input Look Function).
 * Returns the bus data.
 */
uint16_t ethernet_eilfct(struct ethernet *ether);

/* Performs the F1_ETH_EPFCT (Ethernet Post Function).
 * The status is returned via the `output` parameter.
 * Returns TRUE on success.
 */
uint16_t ethernet_epfct(struct ethernet *ether, uint16_t *output);

/* Performs the BS_ETH_EIDFCT (Ethernet Input Data Function).
 * Returns the bus data.
 */
uint16_t ethernet_eidfct(struct ethernet *ether);

/* Performs the F1_ETH_EWFCT (Ethernet Wakeup Function). */
void ethernet_ewfct(struct ethernet *ether);

/* Performs the F2_ETH_EODFCT (Ethernet Output Data Function).
 * The current value of the bus is given by `bus`. The current
 * simulation cycle is given by the parameter `cycle`.
 * Returns TRUE on success.
 */
int ethernet_eodfct(struct ethernet *ether, uint16_t bus, int32_t cycle);

/* Performs the F2_ETH_EOSFCT (Ethernet Output Start Function). */
void ethernet_eosfct(struct ethernet *ether);

/* Performs the F2_ETH_ERBFCT (Ethernet Reset Branch Function).
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t ethernet_erbfct(struct ethernet *ether);

/* Performs the F2_ETH_EEFCT (Ethernet End of transmission Function).
 * The current simulation cycle is given by the parameter `cycle`.
 * Returns TRUE on success.
 */
int ethernet_eefct(struct ethernet *ether, int32_t cycle);

/* Performs the F2_ETH_EBFCT (Ethernet Branch Function).
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t ethernet_ebfct(struct ethernet *ether);

/* Performs the F2_ETH_ECBFCT (Ethernet Countdown Branch Function).
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t ethernet_ecbfct(struct ethernet *ether);

/* Performs the F2_ETH_EISFCT (Ethernet Input Start Function).
 * The current cycle number is given by the parameter `cycle`.
 * Returns TRUE on success.
 */
int ethernet_eisfct(struct ethernet *ether, int32_t cycle);

/* Processes a BLOCK instruction.
 * The task to be blocked is in the parameter `task`.
 */
void ethernet_block_task(struct ethernet *ether, uint8_t task);

/* Processes the ethernet interrupts.
 * Returns TRUE on success.
 */
int ethernet_interrupt(struct ethernet *ether);

/* Runs this before every microinstruction. */
void ethernet_before_step(struct ethernet *ether);

/* Prints the state of the registers.
 * The output is written to decoder `dec` string buffer.
 */
void ethernet_print_registers(const struct ethernet *ether,
                              struct decoder *dec);

/* Serializes the ethernet object to `sd`. */
void ethernet_serialize(const struct ethernet *ether, struct serdes *sd);

/* Deserializes the ethernet object from `sd`. */
void ethernet_deserialize(struct ethernet *ether, struct serdes *sd);


#endif /* __SIMULATOR_ETHERNET_H */
