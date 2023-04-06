
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "simulator/ethernet.h"
#include "simulator/intr.h"
#include "microcode/microcode.h"
#include "common/serdes.h"
#include "common/string_buffer.h"
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

    ether->fifo =
        (uint16_t *) malloc(FIFO_SIZE * sizeof(uint16_t));

    if (unlikely(!ether->fifo)) {
        report_error("ethernet_create: memory exhausted");
        ethernet_destroy(ether);
        return FALSE;
    }

    ether->trp = NULL;
    ether->address = 100;
    return TRUE;
}

void ethernet_set_transport(struct ethernet *ether,
                            struct transport *trp)
{
    ether->trp = trp;
    if (trp) {
        (*trp->clear_tx)(trp->arg);
    }
}

/* Resets the ethernet interface.
 * Returns TRUE on success.
 */
static
int reset_interface(struct ethernet *ether)
{
    int ret;

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

    if (ether->trp) {
        ret = (*ether->trp->enable_rx)(ether->trp->arg, FALSE);
        if (unlikely(!ret)) {
            report_error("ethernet: reset_interface: "
                         "could not disable RX");
            return FALSE;
        }

        /* Clear the RX buffer. */
        (*ether->trp->clear_rx)(ether->trp->arg);
    }

    ether->fifo_start = ether->fifo_end = 0;
    ether->pending &= ~(1 << TASK_ETHERNET);
    return TRUE;
}

/* Updates the intr_cycle. The parameter `cycle` specifies the current
 * cycle. The parameter `must_advance` is passed to the compute_intr_cycle()
 * function.
 * Returns TRUE on success.
 */
static
int update_intr_cycle(struct ethernet *ether,
                      int32_t cycle, int must_advance)
{
    int32_t intr_cycles[2];
    int32_t diff;

    intr_cycles[0] = ether->tx_intr_cycle;
    intr_cycles[1] = ether->rx_intr_cycle;

    /* If the current interrupt cycle is set, use the minimum
     * between intr_cycle and cycle.
     */
    if (ether->intr_cycle >= 0) {
        diff = INTR_CYCLE(cycle - ether->intr_cycle);
        if (!INTR_DIFF_NEG(diff)) {
            cycle = ether->intr_cycle;
        }
    }

    if (unlikely(!compute_intr_cycle(cycle, must_advance,
                                     2, intr_cycles,
                                     &ether->intr_cycle))) {
        report_error("ethernet: update_intr_cycle: "
                     "error in computing interrupt cycle");
        return FALSE;
    }
    return TRUE;
}

/* Starts the transmission of the FIFO data by starting the TX interrupt.
 * The `cycle` parameter indicates the current cycle, and `end_tx` indicates
 * to end the current transmission.
 * Returns TRUE on success.
 */
static
int transmit_fifo(struct ethernet *ether, int32_t cycle, int end_tx)
{
    ether->tx_intr_cycle = INTR_CYCLE(cycle + TX_DURATION);
    ether->end_tx = end_tx;
    if (unlikely(!update_intr_cycle(ether, cycle, FALSE))) {
        report_error("ethernet: transmit_fifo: "
                     "could not update the interrupt cycle");
        return FALSE;
    }
    return TRUE;
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
    } else {
        /* FIFO is full. */
        report_error("ethernet: write_output_fifo: "
                     "FIFO is full");
        return FALSE;
    }

    if (ether->fifo_end >= ether->fifo_start + FIFO_SIZE - 1) {
        if (ether->out_busy) {
            if (unlikely(!transmit_fifo(ether, cycle, FALSE))) {
                report_error("ethernet: write_output_fifo: "
                             "could not update the interrupt cycle");
                return FALSE;
            }
        }
        ether->pending &= ~(1 << TASK_ETHERNET);
    }

    return TRUE;
}

void ethernet_set_address(struct ethernet *ether, uint16_t address)
{
    ether->address = address;
}

int ethernet_reset(struct ethernet *ether)
{
    ether->pending = 0;
    ether->countdown_wakeup = FALSE;
    ether->end_tx = FALSE;

    if (ether->trp) {
        /* Reset the TX buffer. */
        (*ether->trp->clear_tx)(ether->trp->arg);
    }

    ether->intr_cycle = -1;
    ether->tx_intr_cycle = -1;
    ether->rx_intr_cycle = -1;

    if (unlikely(!reset_interface(ether))) {
        report_error("ethernet: reset: "
                     "could not reset interface");
        return FALSE;
    }

    return TRUE;
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

uint16_t ethernet_epfct(struct ethernet *ether, uint16_t *output)
{
    uint16_t out;

    out = ether->status;
    if (unlikely(!reset_interface(ether))) {
        return FALSE;
    }

    if (output) {
        *output = out;
    }

    return TRUE;
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
    if (unlikely(!write_output_fifo(ether, bus, cycle))) {
        report_error("ethernet: eodfct: "
                     "could not write to output FIFO");
        return FALSE;
    }
    return TRUE;
}

void ethernet_eosfct(struct ethernet *ether)
{
    ether->out_busy = TRUE;
    ether->pending |= (1 << TASK_ETHERNET);
}

uint16_t ethernet_erbfct(struct ethernet *ether)
{
    return ((ether->iocmd & 3) << 2);
}

int ethernet_eefct(struct ethernet *ether, int32_t cycle)
{
    if (unlikely(!transmit_fifo(ether, cycle, TRUE))) {
        report_error("ethernet: eefct: "
                     "could not transmit FIFO");
        return FALSE;
    }
    ether->pending &= ~(1 << TASK_ETHERNET);
    return TRUE;
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

int ethernet_eisfct(struct ethernet *ether, int32_t cycle)
{
    int ret;

    if (ether->trp) {
        if (ether->in_busy) {
            /* Clear the current RX packet. */
            (*ether->trp->clear_rx)(ether->trp->arg);
        }
        ret = (*ether->trp->enable_rx)(ether->trp->arg, TRUE);
        if (unlikely(!ret)) {
            report_error("ethernet: eisfct: "
                         "could not enable RX");
            return FALSE;
        }
    }
    ether->input_state = IST_WAITING;
    ether->in_busy = TRUE;

    ether->pending &= ~(1 << TASK_ETHERNET);
    if (ether->rx_intr_cycle < 0) {
        ether->rx_intr_cycle = INTR_CYCLE(cycle + RX_DURATION);
        ret = update_intr_cycle(ether, cycle, FALSE);
        if (unlikely(!ret)) {
            report_error("ethernet: eisfct: "
                         "could not update the interrupt cycle");
            return FALSE;
        }
    }
    return TRUE;
}

void ethernet_block_task(struct ethernet *ether, uint8_t task)
{
    ether->pending &= ~(1 << task);
}

/* Transmission interrupt. */
static
int tx_interrupt(struct ethernet *ether)
{
    uint16_t data;
    int ret;

    ether->tx_intr_cycle = -1;
    if (!ether->out_busy) return TRUE;

    while (ether->fifo_start != ether->fifo_end) {
        data = ether->fifo[ether->fifo_start];
        ether->fifo_start++;
        if (ether->fifo_start >= FIFO_SIZE) {
            ether->fifo_start = 0;
            ether->fifo_end -= FIFO_SIZE;
        }
        if (ether->trp) {
            ret = (*ether->trp->append_tx)(ether->trp->arg, data);
            if (unlikely(!ret)) {
                report_error("ethernet_tx_interrupt: "
                             "could not append to TX buffer");
                return FALSE;
            }
        }
    }
    ether->fifo_start = ether->fifo_end = 0;

    ether->pending |= (1 << TASK_ETHERNET);

    if (ether->end_tx) {
        int ret;
        ether->out_busy = FALSE;
        if (ether->trp) {
            ret = (*ether->trp->send)(ether->trp->arg);
            if (unlikely(!ret)) {
                report_error("ethernet_tx_interrupt: "
                             "could not send packet");
                return FALSE;
            }
        }
    }

    return TRUE;
}

/* Receiving data interrupt. */
static
int rx_interrupt(struct ethernet *ether)
{
    size_t len, rem;
    int is_active;
    int ret;

    if (ether->trp) {
        ret = (*ether->trp->receive)(ether->trp->arg, &len);
        if (unlikely(!ret)) {
            report_error("ethernet_rx_interrupt: "
                         "could not receive packet");
            return FALSE;
        }
    } else {
        len = 0;
    }

    switch (ether->input_state) {
    case IST_WAITING:
        if (len > 0) {
            ether->input_state = IST_RECEIVING;
        }

        is_active = TRUE;
        break;

    case IST_RECEIVING:
        is_active = TRUE;

        if (ether->fifo_end >= ether->fifo_start + FIFO_SIZE) {
            /* FIFO is full, wait for next iteration. */
            break;
        }

        if (ether->trp) {
            rem = (*ether->trp->has_rx_data)(ether->trp->arg);

            if (rem > 0) {
                uint16_t data;
                uint8_t pos;

                data = (*ether->trp->get_rx_data)(ether->trp->arg);
                pos = ether->fifo_end++;
                if (pos >= FIFO_SIZE) pos -= FIFO_SIZE;
                ether->fifo[pos] = data;
            }

            rem = (*ether->trp->has_rx_data)(ether->trp->arg);
            if (rem == 0) {
                ether->in_gone = TRUE;
                (*ether->trp->clear_rx)(ether->trp->arg);
                ether->input_state = IST_DONE;
                ether->pending |= (1 << TASK_ETHERNET);
                is_active = FALSE;
            }
        }

        if (ether->fifo_end >= ether->fifo_start + 2) {
            ether->pending |= (1 << TASK_ETHERNET);
        }

        break;

    case IST_OFF:
        if (ether->trp) {
            /* Clear the RX buffer. */
            (*ether->trp->clear_rx)(ether->trp->arg);
        }

        is_active = FALSE;
        break;

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
    } else {
        ether->rx_intr_cycle = -1;
    }

    return TRUE;
}

int ethernet_interrupt(struct ethernet *ether)
{
    int has_tx, has_rx;

    has_tx = (ether->intr_cycle == ether->tx_intr_cycle);
    has_rx = (ether->intr_cycle == ether->rx_intr_cycle);

    if (has_tx) {
        if (unlikely(!tx_interrupt(ether)))
            return FALSE;
    }
    if (has_rx) {
        if (unlikely(!rx_interrupt(ether)))
            return FALSE;
    }

    if (unlikely(!update_intr_cycle(ether, ether->intr_cycle, TRUE))) {
        report_error("ethernet: interrupt: "
                     "could not update the interrupt cycle");
        return FALSE;
    }
    return TRUE;
}

void ethernet_before_step(struct ethernet *ether)
{
    if (ether->countdown_wakeup) {
        ether->countdown_wakeup = FALSE;
        ether->pending &= ~(1 << TASK_ETHERNET);
    }
}

void ethernet_print_registers(const struct ethernet *ether,
                              struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    decode_tagged_value(dec->vdec, "IOCMD", DECODE_VALUE, ether->iocmd);
    decode_tagged_value(dec->vdec, "FIFOLEN", DECODE_VALUE,
                        ether->fifo_end - ether->fifo_start);
    decode_tagged_value(dec->vdec, "IN_STATE",
                        DECODE_VALUE, ether->input_state);
    decode_tagged_value(dec->vdec, "STATUS",
                        DECODE_VALUE, ether->status);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "OUT_BUSY",
                        DECODE_BOOL, ether->out_busy);
    decode_tagged_value(dec->vdec, "IN_BUSY", DECODE_VALUE, ether->in_busy);
    decode_tagged_value(dec->vdec, "IN_GONE", DECODE_BOOL, ether->in_gone);
    decode_tagged_value(dec->vdec, "END_TX", DECODE_BOOL, ether->end_tx);
    string_buffer_print(output, "\n");

    /* Not simulated. */
    decode_tagged_value(dec->vdec, "DATALATE",
                        DECODE_BOOL, ether->data_late);
    decode_tagged_value(dec->vdec, "COLL",
                        DECODE_BOOL, ether->collision);
    decode_tagged_value(dec->vdec, "CRC_BAD",
                        DECODE_BOOL, ether->crc_bad);
    decode_tagged_value(dec->vdec, "INCOMPL",
                        DECODE_BOOL, ether->incomplete);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "ADDRESS",
                        DECODE_VALUE, ether->address);
    decode_tagged_value(dec->vdec, "CT_WAKEUP",
                        DECODE_BOOL, ether->countdown_wakeup);
    decode_tagged_value(dec->vdec, "PEND", DECODE_VALUE, ether->pending);
    decode_tagged_value(dec->vdec, "ICYC",
                        DECODE_SVALUE32, ether->intr_cycle);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "TX_ICYC",
                        DECODE_SVALUE32, ether->tx_intr_cycle);
    decode_tagged_value(dec->vdec, "RX_ICYC",
                        DECODE_SVALUE32, ether->rx_intr_cycle);
    string_buffer_print(output, "\n");
}

void ethernet_serialize(const struct ethernet *ether, struct serdes *sd)
{
    serdes_put16(sd, ether->address);
    serdes_put16_array(sd, ether->fifo, FIFO_SIZE);
    serdes_put8(sd, ether->fifo_start);
    serdes_put8(sd, ether->fifo_end);
    serdes_put16(sd, ether->iocmd);
    serdes_put_bool(sd, ether->out_busy);
    serdes_put_bool(sd, ether->in_busy);
    serdes_put_bool(sd, ether->in_gone);
    serdes_put16(sd, ether->input_state);
    serdes_put_bool(sd, ether->data_late);
    serdes_put_bool(sd, ether->collision);
    serdes_put_bool(sd, ether->crc_bad);
    serdes_put_bool(sd, ether->incomplete);
    serdes_put16(sd, ether->status);
    serdes_put_bool(sd, ether->countdown_wakeup);
    serdes_put_bool(sd, ether->end_tx);
    serdes_put32(sd, ether->intr_cycle);
    serdes_put32(sd, ether->tx_intr_cycle);
    serdes_put32(sd, ether->rx_intr_cycle);
    serdes_put16(sd, ether->pending);
}

void ethernet_deserialize(struct ethernet *ether, struct serdes *sd)
{
    ether->address = serdes_get16(sd);
    serdes_get16_array(sd, ether->fifo, FIFO_SIZE);
    ether->fifo_start = serdes_get8(sd);
    ether->fifo_end = serdes_get8(sd);
    ether->iocmd = serdes_get16(sd);
    ether->out_busy = serdes_get_bool(sd);
    ether->in_busy = serdes_get_bool(sd);
    ether->in_gone = serdes_get_bool(sd);
    ether->input_state = serdes_get16(sd);
    ether->data_late = serdes_get_bool(sd);
    ether->collision = serdes_get_bool(sd);
    ether->crc_bad = serdes_get_bool(sd);
    ether->incomplete = serdes_get_bool(sd);
    ether->status = serdes_get16(sd);
    ether->countdown_wakeup = serdes_get_bool(sd);
    ether->end_tx = serdes_get_bool(sd);
    ether->intr_cycle = serdes_get32(sd);
    ether->tx_intr_cycle = serdes_get32(sd);
    ether->rx_intr_cycle = serdes_get32(sd);
    ether->pending = serdes_get16(sd);
}
