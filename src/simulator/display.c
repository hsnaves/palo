
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "simulator/display.h"
#include "simulator/intr.h"
#include "microcode/microcode.h"
#include "common/serdes.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Constants. */
/* Video Oscillator: 20.16 MHz
 * Total number of pixels per line: 24 * 32 = 768
 * Total visible pixels per line: 606
 * Line frequency: 20.16 MHz / 768 = 26.25 KHz (period = 38.095 us)
 * Total number of lines: 875
 * Number of visible lines: 808
 * Vertical SYNC frequency: 26.25 KHz / (875 / 2) = 60 Hz
 *                          [ factor of 2 for interlaced signal ]
 * H-Blank period: time of 6 * 24 = 144 pixels = 7.1428 us
 *
 * Original Alto clock settings:
 * Main Clock Oscillator: 29.4 MHz
 * Main Clock: (29.4 MHz) / 5 = 5.88 MHz (period = 170.068 ns)
 *
 * We switch the main clock to 6.3MHz to be multiple of video oscillator.
 * The period of the clock is then:  158.73ns
 */
#define FIFO_SIZE                         16
#define SCANLINE_WORDS                    38  /* Visible at full bit clock. */
#define SCANLINE_DURATION                240  /*    38 us / 150.73 ns */
#define HBLANK_DURATION                   45  /*  7.14 us / 158.73 ns */
#define SCANLINE_VISIBLE_DURATION  (SCANLINE_DURATION - HBLANK_DURATION)
#define WORD_DURATION                      5  /*   793 us / 158.73 ns */
#define VBLANK_SCANLINES_EVEN             33
#define VBLANK_SCANLINES_ODD              34
#define VISIBLE_LINES_PER_FIELD          404

#define MODE_LOWRES                   0x8000
#define MODE_WOB                      0x4000

/* Functions. */

void display_initvar(struct display *displ)
{
    displ->display_data = NULL;
    displ->fifo = NULL;
}

void display_destroy(struct display *displ)
{
    if (displ->display_data) free((void *) displ->display_data);
    displ->display_data = NULL;

    if (displ->fifo) free((void *) displ->fifo);
    displ->fifo = NULL;
}

int display_create(struct display *displ)
{
    display_initvar(displ);

    displ->fifo = (uint16_t *) malloc(FIFO_SIZE * sizeof(uint16_t));
    displ->display_data = (uint8_t *)
        malloc(DISPLAY_DATA_SIZE * sizeof(uint8_t));

    if (unlikely(!displ->fifo || !displ->display_data)) {
        report_error("display: create: memory exhausted");
        display_destroy(displ);
        return FALSE;
    }

    display_reset(displ);
    return TRUE;
}

void display_reset(struct display *displ)
{
    displ->fifo_start = displ->fifo_end = 0;

    displ->even_field = FALSE;
    displ->hblank = FALSE;
    displ->scanline = 0;
    displ->word = 0;

    displ->cursor_x = 0;
    displ->cursor_data = 0;
    displ->cursor_x_latched = 0;
    displ->cursor_data_latched = 0;

    displ->low_res = FALSE;
    displ->low_res_latched = FALSE;
    displ->wob = FALSE;
    displ->wob_latched = FALSE;

    displ->dh_blocked = FALSE;
    displ->dw_blocked = FALSE;
    displ->cur_blocked = FALSE;

    displ->dw_intr_cycle = 0xFFFFFFFFU;
    displ->dhl_intr_cycle = SCANLINE_VISIBLE_DURATION;
    displ->intr_cycle = displ->dhl_intr_cycle;
    displ->pending = 0;
}

/* Returns the scanline where the vblanking ends.
 * Returns the scanline number of the vblanking threshold.
 */
static
uint16_t vblank_threshold(struct display *displ)
{
    return displ->even_field ? VBLANK_SCANLINES_EVEN : VBLANK_SCANLINES_ODD;
}

/* Checks if the FIFO is empty.
 * Returns TRUE is the FIFO is empty.
 */
static
int is_fifo_empty(struct display *displ)
{
    return (displ->fifo_end <= displ->fifo_start);
}

/* Checks if the FIFO is full.
 * Returns TRUE is the FIFO is full.
 */
static
int is_fifo_full(struct display *displ)
{
    return (displ->fifo_end >= displ->fifo_start + FIFO_SIZE);
}

/* Checks if the FIFO is almost full.
 * Returns TRUE is the FIFO is almost full.
 */
static
int is_fifo_almost_full(struct display *displ)
{
    return (displ->fifo_end >= displ->fifo_start + FIFO_SIZE - 4);
}

int display_load_ddr(struct display *displ, uint16_t bus)
{
    uint8_t pos;
    int fifo_full;

    fifo_full = is_fifo_full(displ);
    if (!fifo_full) {
        pos = displ->fifo_end++;
        if (pos >= FIFO_SIZE) pos -= FIFO_SIZE;
        displ->fifo[pos] = bus;
    } else {
        report_error("display: load_ddr: buffer full");
        return FALSE;
    }

    if (is_fifo_almost_full(displ)) {
        displ->pending &= ~(1 << TASK_DISPLAY_WORD);
    }
    return TRUE;
}

void display_load_xpreg(struct display *displ, uint16_t bus)
{
    displ->cursor_x = (~bus); /* negate value from bus. */
}

void display_load_csr(struct display *displ, uint16_t bus)
{
    displ->cursor_data = bus;
}

uint16_t display_even_field(struct display *displ)
{
    return (displ->even_field) ? 1 : 0;
}

uint16_t display_set_mode(struct display *displ, uint16_t bus)
{
    displ->low_res = ((bus & MODE_LOWRES) != 0);
    displ->wob = ((bus & MODE_WOB) != 0);
    return (bus & MODE_LOWRES) ? 1 : 0;
}

void display_block_task(struct display *displ, uint8_t task)
{
    if (task == TASK_DISPLAY_WORD) {
        displ->dw_blocked = TRUE;
        if (!displ->dh_blocked) {
            displ->pending |= (1 << TASK_DISPLAY_HORIZONTAL);
        }
    } else if (task == TASK_DISPLAY_HORIZONTAL) {
        displ->dh_blocked = TRUE;
        displ->pending &= ~(1 << TASK_DISPLAY_WORD);
    } else if (task == TASK_CURSOR) {
        displ->cur_blocked = TRUE;
    }
    displ->pending &= ~(1 << task);
}

/* Display half-line interrupt routine. */
static
void dhl_interrupt(struct display *displ)
{
    uint16_t vblank_thresh;
    int almost_full, vblank;

    if (displ->hblank) {
        vblank_thresh = vblank_threshold(displ);

        displ->scanline++;
        if (displ->scanline >= vblank_thresh + VISIBLE_LINES_PER_FIELD) {
            displ->scanline = 0;
            displ->dh_blocked = FALSE;
            displ->cur_blocked = FALSE;
            displ->even_field = 1 - displ->even_field;

            displ->pending |= (1 << TASK_DISPLAY_VERTICAL)
                | (1 << TASK_DISPLAY_HORIZONTAL);
        }

        displ->word = 0;

        displ->cursor_x_latched = displ->cursor_x;
        displ->cursor_data_latched = displ->cursor_data;
        displ->cursor_x = 0;
        displ->cursor_data = 0;

        displ->low_res_latched = displ->low_res;
        displ->wob_latched = displ->wob;

        displ->hblank = FALSE;
        displ->dhl_intr_cycle =
            INTR_CYCLE(displ->intr_cycle + SCANLINE_VISIBLE_DURATION);
    } else {
         /* Wakup the memory refresh task (and possibly the
          * ethernet task).
          */
        displ->pending |= (1 << TASK_MEMORY_REFRESH)
            | (1 << TASK_ETHERNET);

        /* Also wakeup the cursor task, if not blocked. */
        if (!displ->cur_blocked) {
            displ->pending |= (1 << TASK_CURSOR);
        }

        /* Clear the buffers. */
        displ->fifo_start = displ->fifo_end = 0;
        displ->dw_blocked = FALSE;

        displ->hblank = TRUE;
        displ->dhl_intr_cycle =
            INTR_CYCLE(displ->intr_cycle + HBLANK_DURATION);
    }

    /* Check if the word task should be awakened. */
    almost_full = is_fifo_almost_full(displ);
    vblank_thresh = vblank_threshold(displ);
    vblank = (displ->scanline < vblank_thresh);

    if (almost_full || vblank || displ->hblank
        || displ->dh_blocked || displ->dw_blocked) {
        displ->pending &= ~(1 << TASK_DISPLAY_WORD);
    } else {
        displ->pending |= (1 << TASK_DISPLAY_WORD);
    }

    if (!vblank && !displ->hblank) {
        /* One word that is always skipped (see schematics of buffer control),
         * pluts the time of the first word to be displayed.
         */
        if (displ->low_res_latched) {
            displ->dw_intr_cycle =
                INTR_CYCLE(displ->intr_cycle + 4 * WORD_DURATION);
        } else {
            displ->dw_intr_cycle =
                INTR_CYCLE(displ->intr_cycle + 2 * WORD_DURATION);
        }
    }
}


/* Display word interrupt routine. */
static
void dw_interrupt(struct display *displ)
{
    uint16_t adj_scanline;
    uint16_t to_display, d;
    uint16_t x_offset, x;
    uint8_t *data, data1;
    int i, almost_full;

    if (displ->even_field) {
        adj_scanline = 2 * (displ->scanline - VBLANK_SCANLINES_EVEN);
    } else {
        adj_scanline = 2 * (displ->scanline - VBLANK_SCANLINES_ODD) + 1;
    }

    if (!is_fifo_empty(displ)) {
        to_display = displ->fifo[displ->fifo_start++];
        if (displ->fifo_start == FIFO_SIZE) {
            displ->fifo_start = 0;
            displ->fifo_end -= FIFO_SIZE;
        }
    } else {
        to_display = 0;
    }

    almost_full = is_fifo_almost_full(displ);
    if (almost_full || displ->dh_blocked || displ->dw_blocked) {
        displ->pending &= ~(1 << TASK_DISPLAY_WORD);
    } else {
        displ->pending |= (1 << TASK_DISPLAY_WORD);
    }

    if (!displ->wob_latched)
        to_display = ~to_display;

    x_offset = displ->word * 16;
    if (displ->low_res_latched)
        x_offset *= 2;

    /* Display the to_display word. */
    data = &displ->display_data[adj_scanline * DISPLAY_STRIDE];
    x = x_offset;
    d = to_display;
    for (i = 0; i < 16; i++) {
        data1 = (d & 0x8000) ? 0xFF : 0x00;
        data[x++] = data1;
        if (displ->low_res_latched) {
            data[x++] = data1;
        }
        d <<= 1;
    }

    displ->word++;
    if (!(displ->hblank)) {
        /* More words to process. */
        displ->dw_intr_cycle = displ->intr_cycle;
        displ->dw_intr_cycle += (displ->low_res_latched)
            ? 2 * WORD_DURATION : WORD_DURATION;
        displ->dw_intr_cycle = INTR_CYCLE(displ->dw_intr_cycle);
        return;
    }

    /* We are the end of a scanline. */
    displ->dw_intr_cycle = -1;

    if (displ->cursor_x_latched < DISPLAY_STRIDE) {
        /* Draw cursor. */
        x = displ->cursor_x_latched;
        d = displ->cursor_data_latched;
        for (i = 0; i < 16; i++) {
            data1 = (d & 0x8000) ? 0xFF : 0x00;
            if (displ->wob_latched) {
                data[x++] |= data1;
            } else {
                data[x++] &= ~data1;
            }
            if (x >= DISPLAY_STRIDE) break;
            d <<= 1;
        }
    }
}

/* Updates the intr_cycle.
 * Returns TRUE on success.
 */
static
int update_intr_cycle(struct display *displ)
{
    int32_t intr_cycles[2];

    intr_cycles[0] = displ->dhl_intr_cycle;
    intr_cycles[1] = displ->dw_intr_cycle;

    if (unlikely(!compute_intr_cycle(displ->intr_cycle, TRUE,
                                     2, intr_cycles,
                                     &displ->intr_cycle))) {
        report_error("display: update_intr_cycle: "
                     "error in computing interrupt cycle");
        return FALSE;
    }
    return TRUE;
}

int display_interrupt(struct display *displ)
{
    int has_dhl, has_dw;

    has_dhl = (displ->intr_cycle == displ->dhl_intr_cycle);
    has_dw = (displ->intr_cycle == displ->dw_intr_cycle);

    if (has_dhl) dhl_interrupt(displ);
    if (has_dw) dw_interrupt(displ);

    return update_intr_cycle(displ);
}

void display_on_switch_task(struct display *displ, uint8_t task)
{
    if (task == TASK_DISPLAY_WORD)
        return;

    /* Automatically blocks task on switch. */
    displ->pending &= ~(1 << task);
}

void display_print_registers(const struct display *displ,
                             struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    decode_tagged_value(dec->vdec, "SCANLINE",
                        DECODE_VALUE, displ->scanline);
    decode_tagged_value(dec->vdec, "WORD", DECODE_MEMORY, displ->word);
    decode_tagged_value(dec->vdec, "EVENFIELD",
                        DECODE_BOOL, displ->even_field);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "CUR_X", DECODE_VALUE, displ->cursor_x);
    decode_tagged_value(dec->vdec, "CUR_DAT",
                        DECODE_VALUE, displ->cursor_data);
    decode_tagged_value(dec->vdec, "CUR_X_L",
                        DECODE_VALUE, displ->cursor_x_latched);
    decode_tagged_value(dec->vdec, "CUR_DAT_L",
                        DECODE_VALUE, displ->cursor_data_latched);
    string_buffer_print(output, "\n");


    decode_tagged_value(dec->vdec, "LOWRES", DECODE_BOOL, displ->low_res);
    decode_tagged_value(dec->vdec, "LOWRES_L",
                        DECODE_BOOL, displ->low_res_latched);
    decode_tagged_value(dec->vdec, "WOB", DECODE_BOOL, displ->wob);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "WOB_L",
                        DECODE_BOOL, displ->wob_latched);
    decode_tagged_value(dec->vdec, "DH_BLOCK",
                        DECODE_BOOL, displ->dh_blocked);
    decode_tagged_value(dec->vdec, "PEND",
                        DECODE_VALUE, displ->pending);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "ICYC",
                        DECODE_SVALUE32, displ->intr_cycle);
    decode_tagged_value(dec->vdec, "DHL_ICYC",
                        DECODE_SVALUE32, displ->dhl_intr_cycle);
    decode_tagged_value(dec->vdec, "DW_ICYC",
                        DECODE_SVALUE32, displ->dw_intr_cycle);
    string_buffer_print(output, "\n");
}

void display_serialize(const struct display *displ, struct serdes *sd)
{
    serdes_put16_array(sd, displ->fifo, FIFO_SIZE);
    serdes_put8(sd, displ->fifo_start);
    serdes_put8(sd, displ->fifo_end);
    serdes_put_bool(sd, displ->even_field);
    serdes_put_bool(sd, displ->hblank);
    serdes_put16(sd, displ->scanline);
    serdes_put16(sd, displ->word);
    serdes_put16(sd, displ->cursor_x);
    serdes_put16(sd, displ->cursor_data);
    serdes_put16(sd, displ->cursor_x_latched);
    serdes_put16(sd, displ->cursor_data_latched);
    serdes_put_bool(sd, displ->low_res);
    serdes_put_bool(sd, displ->low_res_latched);
    serdes_put_bool(sd, displ->wob);
    serdes_put_bool(sd, displ->wob_latched);
    serdes_put_bool(sd, displ->dh_blocked);
    serdes_put_bool(sd, displ->dw_blocked);
    serdes_put_bool(sd, displ->cur_blocked);
    serdes_put32(sd, displ->intr_cycle);
    serdes_put32(sd, displ->dhl_intr_cycle);
    serdes_put32(sd, displ->dw_intr_cycle);
    serdes_put16(sd, displ->pending);
}

void display_deserialize(struct display *displ, struct serdes *sd)
{
    serdes_get16_array(sd, displ->fifo, FIFO_SIZE);
    displ->fifo_start = serdes_get8(sd);
    displ->fifo_end = serdes_get8(sd);
    displ->even_field = serdes_get_bool(sd);
    displ->hblank = serdes_get_bool(sd);
    displ->scanline = serdes_get16(sd);
    displ->word = serdes_get16(sd);
    displ->cursor_x = serdes_get16(sd);
    displ->cursor_data = serdes_get16(sd);
    displ->cursor_x_latched = serdes_get16(sd);
    displ->cursor_data_latched = serdes_get16(sd);
    displ->low_res = serdes_get_bool(sd);
    displ->low_res_latched = serdes_get_bool(sd);
    displ->wob = serdes_get_bool(sd);
    displ->wob_latched = serdes_get_bool(sd);
    displ->dh_blocked = serdes_get_bool(sd);
    displ->dw_blocked = serdes_get_bool(sd);
    displ->cur_blocked = serdes_get_bool(sd);
    displ->intr_cycle = serdes_get32(sd);
    displ->dhl_intr_cycle = serdes_get32(sd);
    displ->dw_intr_cycle = serdes_get32(sd);
    displ->pending = serdes_get16(sd);
}
