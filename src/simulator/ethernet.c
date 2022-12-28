
#include <stdint.h>
#include <stdlib.h>

#include "simulator/ethernet.h"
#include "simulator/utils.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define FIFO_SIZE                         16
#define TX_DURATION                      512
#define RX_DURATION                       31

#define IST_OFF                            0
#define IST_WAITING                        1
#define IST_RECEIVING                      2
#define IST_DONE                           3

/* Functions. */

void ethernet_initvar(struct ethernet *ether)
{
    ether->fifo = NULL;
}

void ethernet_destroy(struct ethernet *ether)
{
    if (ether->fifo) free((void *) ether->fifo);
    ether->fifo = NULL;
}

int ethernet_create(struct ethernet *ether)
{
    ethernet_initvar(ether);

    ether->fifo = (uint16_t *) malloc(FIFO_SIZE * sizeof(uint16_t));

    if (unlikely(!ether->fifo)) {
        report_error("ethernet_create: memory exhausted");
        ethernet_destroy(ether);
        return FALSE;
    }

    ethernet_reset(ether);
    return TRUE;
}

/* Resets the ethernet interface. */
static
void reset_interface(struct ethernet *ether)
{
    ether->status = 0xFFC0;
    ether->status |= (ether->data_late) ? 0x00 : 0x20;
    ether->status |= (ether->collision) ? 0x00 : 0x10;
    ether->status |= (ether->crc_bad) ? 0x00 : 0x08;
    ether->status |= (0x3 << 1);
    ether->status |= (ether->incomplete) ? 0x00 : 0x01;

    ether->iocmd = 0;
    ether->out_busy = FALSE;
    ether->in_busy = FALSE;
    ether->in_gone = FALSE;
    ether->data_late = FALSE; /* Not simulated. */
    ether->collision = FALSE; /* Not simulated. */
    ether->crc_bad = FALSE; /* Not simulated. */
    ether->incomplete = FALSE; /* Not simulated. */

    ether->input_state = IST_OFF;

    ether->fifo_start = ether->fifo_end = 0;
    ether->pending &= ~(1 << TASK_ETHERNET);
}

static
void transmit_fifo(struct ethernet *ether, int32_t cycle, int end_tx)
{
    ether->tx_intr_cycle = INTR_CYCLE(cycle + TX_DURATION);
    ether->end_tx = end_tx;
}

/* Obtains a word from the input fifo.
 * If `peek` is set to TRUE, the word is not removed from the FIFO.
 * Returns the word from the fifo.
 */
static
uint16_t read_input_fifo(struct ethernet *ether, int peek)
{
    uint16_t output;

    /* If empty, returns 0 as output. */
    if (ether->fifo_start == ether->fifo_end)
        return 0;

    output = ether->fifo[ether->fifo_start];
    if (peek) return output;

    ether->fifo_start++;
    if (ether->fifo_start >= FIFO_SIZE) {
        ether->fifo_start = 0;
        ether->fifo_end -= FIFO_SIZE;
    }

    if (ether->fifo_end < ether->fifo_start + 2) {
        if (ether->in_gone) {
            ether->in_busy = FALSE;
            ether->pending |= (1 << TASK_ETHERNET);
        } else {
            ether->pending &= ~(1 << TASK_ETHERNET);
        }
    }
    return output;
}

/* Writes a word to the output fifo.
 * The word to be written is in `bus`, and the current simulation
 * cycle is given by the parameter `cycle`.
 * Returns TRUE on success.
 */
static
int write_output_fifo(struct ethernet *ether, uint16_t bus,
                      int32_t cycle)
{
    uint8_t pos;
    if (ether->fifo_end < ether->fifo_start + FIFO_SIZE) {
        pos = ether->fifo_end++;
        if (pos >= FIFO_SIZE) pos -= FIFO_SIZE;
        ether->fifo[pos] = bus;
    }

    if (ether->fifo_end >= ether->fifo_start + FIFO_SIZE - 1) {
        if (ether->out_busy) {
            transmit_fifo(ether, cycle, FALSE);
        }
        ether->pending &= ~(1 << TASK_ETHERNET);
    }

    return TRUE;
}

static
void start_output(struct ethernet *ether)
{
    ether->out_busy = TRUE;
    ether->pending |= (1 << TASK_ETHERNET);
}

static
void start_input(struct ethernet *ether, int32_t cycle)
{
    ether->input_state = IST_WAITING;
    ether->in_busy = TRUE;

    ether->pending &= ~(1 << TASK_ETHERNET);
    if (ether->rx_intr_cycle < 0) {
        ether->rx_intr_cycle = INTR_CYCLE(cycle + RX_DURATION);
    }
}

static
void end_transmission(struct ethernet *ether, int32_t cycle)
{
    transmit_fifo(ether, cycle, TRUE);
    ether->pending &= ~(1 << TASK_ETHERNET);
}

void ethernet_reset(struct ethernet *ether)
{
    ether->pending = 0;
    ether->address = 64;
    ether->countdown_wakeup = FALSE;
    ether->end_tx = FALSE;

    ether->intr_cycle = -1;
    ether->tx_intr_cycle = -1;
    ether->rx_intr_cycle = -1;

    reset_interface(ether);
}

uint16_t ethernet_rsnf(struct ethernet *ether)
{
    return (0xFF00U | ether->address);
}

void ethernet_startf(struct ethernet *ether, uint16_t bus)
{
    ether->iocmd = (bus & 0x03);
    ether->pending |= (1 << TASK_ETHERNET);
}

uint16_t ethernet_eilfct(struct ethernet *ether)
{
    return read_input_fifo(ether, TRUE);
}

uint16_t ethernet_epfct(struct ethernet *ether)
{
    uint16_t output;

    output = ether->status;
    reset_interface(ether);

    return output;
}

uint16_t ethernet_eidfct(struct ethernet *ether)
{
    return read_input_fifo(ether, FALSE);
}

void ethernet_ewfct(struct ethernet *ether)
{
    ether->countdown_wakeup = TRUE;
}

int ethernet_eodfct(struct ethernet *ether, uint16_t bus, int32_t cycle)
{
    return write_output_fifo(ether, bus, cycle);
}

void ethernet_eosfct(struct ethernet *ether)
{
    start_output(ether);
}

uint16_t ethernet_erbfct(struct ethernet *ether)
{
    return ((ether->iocmd & 3) << 2);
}

void ethernet_eefct(struct ethernet *ether, int32_t cycle)
{
    end_transmission(ether, cycle);
}

uint16_t ethernet_ebfct(struct ethernet *ether)
{
    uint16_t next_extra;
    int oper_done;

    next_extra = 0;
    oper_done = (!ether->in_busy && !ether->out_busy);
    if (ether->data_late || ether->iocmd != 0 || oper_done) {
        next_extra |= 0x0004;
    }
    if (ether->collision) {
        next_extra |= 0x0008;
    }
    return next_extra;
}

uint16_t ethernet_ecbfct(struct ethernet *ether)
{
    if (ether->fifo_start != ether->fifo_end) {
        return 0x04;
    }
    return 0x0;
}

void ethernet_eisfct(struct ethernet *ether, int32_t cycle)
{
    start_input(ether, cycle);
}

void ethernet_block_task(struct ethernet *ether, uint8_t task)
{
    ether->pending &= ~(1 << task);
}

/* Transmission interrupt. */
static
void tx_interrupt(struct ethernet *ether)
{
    if (!ether->out_busy) return;

    /* TODO: the data is going to limbo, use it for something? */
    ether->fifo_start = ether->fifo_end = 0;

    ether->pending |= (1 << TASK_ETHERNET);

    if (ether->end_tx) {
        ether->out_busy = FALSE;
    }
}

/* Receiving data interrupt. */
static
void rx_interrupt(struct ethernet *ether)
{
    int is_active;

    switch (ether->input_state) {
    case IST_WAITING:
        /* TODO: Implement this. */
        is_active = TRUE;
        break;

    case IST_RECEIVING:
        /* TODO: Implement this. */
        is_active = TRUE;
        break;

    case IST_OFF:
    case IST_DONE:
        is_active = FALSE;
        break;

    default:
        is_active = FALSE;
        break;
    }

    if (is_active) {
        ether->rx_intr_cycle =
            INTR_CYCLE(ether->intr_cycle + RX_DURATION);
    }
}

/* Updates the intr_cycle. */
static
void update_intr_cycle(struct ethernet *ether)
{
    int32_t intr_cycles[2];

    intr_cycles[0] = ether->tx_intr_cycle;
    intr_cycles[1] = ether->rx_intr_cycle;

    ether->intr_cycle = compute_intr_cycle(ether->intr_cycle,
                                           2, intr_cycles);
}

void ethernet_interrupt(struct ethernet *ether)
{
    int has_tx, has_rx;

    has_tx = (ether->intr_cycle == ether->tx_intr_cycle);
    has_rx = (ether->intr_cycle == ether->rx_intr_cycle);

    if (has_tx) tx_interrupt(ether);
    if (has_rx) rx_interrupt(ether);

    update_intr_cycle(ether);
}

void ethernet_before_step(struct ethernet *ether)
{
    if (ether->countdown_wakeup) {
        ether->countdown_wakeup = FALSE;
        ether->pending &= ~(1 << TASK_ETHERNET);
    }
}

void ethernet_print_registers(struct ethernet *ether,
                              struct string_buffer *output)
{
    /* TODO: Implement this. */
    UNUSED(ether);
    string_buffer_print(output, "<empty>");
}
