
#include <stdint.h>
#include <stdlib.h>

#include "simulator/display.h"
#include "simulator/utils.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define FIFO_SIZE                    16
#define SCANLINE_WORDS               38
#define VBLANK_DURATION            3911  /*   665 us / 170 ns */
#define SCANLINE_DURATION           224  /*    38 us / 170 ns */
#define HBLANK_DURATION              35  /*     6 us / 170 ns */
#define WORD_DURATION                 5  /* 0.842 us / 170 ns */

#define MODE_LOWRES              0x8000
#define MODE_WOB                 0x4000
#define FIRST_BIT                0x8000

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
    displ->scanline = 0;
    displ->vblank_scanline = 0;
    displ->word = 0;

    displ->cursor_x = 0;
    displ->cursor_x_latched = 0;
    displ->has_cursor_x = FALSE;

    displ->cursor_data = 0;
    displ->cursor_data_latched = 0;
    displ->has_cursor_data = FALSE;

    displ->switch_mode = FALSE;
    displ->low_res = FALSE;
    displ->low_res_latched = FALSE;
    displ->wob = FALSE;
    displ->wob_latched = FALSE;

    displ->dw_blocked = TRUE;
    displ->dh_blocked = FALSE;

    displ->intr_cycle = VBLANK_DURATION;
    displ->dw_intr_cycle = 0xFFFFFFFFU;
    displ->dh_intr_cycle = 0xFFFFFFFFU;
    displ->dv_intr_cycle = VBLANK_DURATION;
    displ->pending = 0;
}

/* Updates the pending status for the display word task. */
static
void check_dw_pending(struct display *displ)
{
    int fifo_full;

    fifo_full = (displ->fifo_end >= displ->fifo_start + FIFO_SIZE);
    if (fifo_full || displ->dh_blocked || displ->dw_blocked) {
        displ->pending &= ~(1 << TASK_DISPLAY_WORD);
    } else {
        displ->pending |= (1 << TASK_DISPLAY_WORD);
    }
}

void display_load_ddr(struct display *displ, uint16_t bus)
{
    uint8_t pos;
    int fifo_full;

    fifo_full = (displ->fifo_end >= displ->fifo_start + FIFO_SIZE);
    if (!fifo_full) {
        pos = displ->fifo_end++;
        if (pos >= FIFO_SIZE) pos -= FIFO_SIZE;
        displ->fifo[pos] = bus;
    }
    check_dw_pending(displ);
}

void display_load_xpreg(struct display *displ, uint16_t bus)
{
    if (displ->has_cursor_x) return;
    displ->cursor_x = (~bus); /* negate value from bus. */
    displ->has_cursor_x = TRUE;
}

void display_load_csr(struct display *displ, uint16_t bus)
{
    if (displ->has_cursor_data) return;
    displ->cursor_data = bus;
    displ->has_cursor_data = TRUE;
}

uint16_t display_even_field(struct display *displ)
{
    return (displ->even_field) ? 1 : 0;
}

uint16_t display_set_mode(struct display *displ, uint16_t bus)
{
    displ->low_res = ((bus & MODE_LOWRES) != 0);
    displ->wob = ((bus & MODE_WOB) != 0);
    displ->switch_mode = TRUE;
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
    }
    displ->pending &= ~(1 << task);
}

/* Display vertical interrupt routine. */
static
void dv_interrupt(struct display *displ)
{
    int vblank_thresh;
    displ->vblank_scanline++;

    /* Wakup the memory refresh task. */
    displ->pending |= (1 << TASK_MEMORY_REFRESH);

    vblank_thresh = displ->even_field ? 33 : 34;

    if (displ->vblank_scanline > vblank_thresh) {

        displ->fifo_start = displ->fifo_end = 0;

        displ->dw_blocked = FALSE;
        displ->dh_blocked = FALSE;

        displ->pending |= ((1 << TASK_DISPLAY_HORIZONTAL)
                           | (1 << TASK_DISPLAY_WORD)
                           | (1 << TASK_CURSOR));

        displ->dh_intr_cycle =
            INTR_CYCLE(displ->intr_cycle + HBLANK_DURATION);
        displ->dv_intr_cycle = -1; /* Disable this interrupt. */
    } else {
        displ->dv_intr_cycle =
            INTR_CYCLE(displ->intr_cycle + SCANLINE_DURATION);
    }
}

/* Display horizontal interrupt routine. */
static
void dh_interrupt(struct display *displ)
{
    displ->word = 0;
    if (displ->has_cursor_x) {
        displ->cursor_x_latched = displ->cursor_x;
        displ->has_cursor_x = FALSE;
    }

    if (displ->has_cursor_data) {
        displ->cursor_data_latched = displ->cursor_data;
        displ->has_cursor_data = FALSE;
    }

    displ->dw_intr_cycle =
        INTR_CYCLE(displ->intr_cycle + 2 * WORD_DURATION);
    displ->dh_intr_cycle = -1; /* Disable the interrupt. */
}

/* Starts a new field. */
static
void field_start(struct display *displ)
{
    displ->even_field = !displ->even_field;

    displ->pending |= (1 << TASK_DISPLAY_VERTICAL);
    displ->pending &= ~((1 << TASK_DISPLAY_HORIZONTAL)
                        | (1 << TASK_DISPLAY_WORD));

    displ->scanline = (displ->even_field) ? 1 : 0;
    displ->vblank_scanline = 0;

    displ->fifo_start = displ->fifo_end = 0;

    displ->dv_intr_cycle =
        INTR_CYCLE(displ->intr_cycle + VBLANK_DURATION);
}

/* Display word interrupt routine. */
static
void dw_interrupt(struct display *displ)
{
    uint16_t to_display, tmp;
    uint16_t x_offset, x;
    uint8_t *data, data1;
    int i, word_thresh;

    if (displ->fifo_end > displ->fifo_start) {
        to_display = displ->fifo[displ->fifo_start++];
        if (displ->fifo_start == FIFO_SIZE) {
            displ->fifo_start = 0;
            displ->fifo_end -= FIFO_SIZE;
        }
        check_dw_pending(displ);
    } else {
        to_display = 0;
    }

    if (!displ->wob_latched)
        to_display = ~to_display;

    x_offset = displ->word * 16;
    if (displ->low_res_latched)
        x_offset *= 2;

    /* Display the to_display word. */
    data = &displ->display_data[displ->scanline * DISPLAY_STRIDE];
    x = x_offset;
    tmp = to_display;
    for (i = 0; i < 16; i++) {
        data1 = (tmp & FIRST_BIT) ? 0xFF : 0x00;
        data[x++] = data1;
        if (displ->low_res_latched) {
            data[x++] = data1;
        }
        tmp <<= 1;
    }

    displ->word++;

    word_thresh = (displ->low_res_latched)
        ? (SCANLINE_WORDS / 2) : SCANLINE_WORDS;

    if (displ->word < word_thresh) {
        /* More words to process. */
        displ->dw_intr_cycle = displ->intr_cycle;
        displ->dw_intr_cycle += (displ->low_res_latched)
            ? 2 * WORD_DURATION : WORD_DURATION;
        displ->dw_intr_cycle = INTR_CYCLE(displ->dw_intr_cycle);
        return;
    }

    /* We are the end of a scanline. */
    displ->dw_intr_cycle = -1;

    if (displ->cursor_x_latched < DISPLAY_WIDTH) {
        /* Draw cursor. */
        x = displ->cursor_x_latched;
        tmp = displ->cursor_data_latched;
        for (i = 0; i < 16; i++) {
            data1 = (tmp & FIRST_BIT) ? 0xFF : 0x00;
            if (displ->wob_latched) {
                data[x++] &= data1;
            } else {
                data[x++] |= data1;
            }
            if (x >= DISPLAY_WIDTH) break;
            tmp <<= 1;
        }
    }

    displ->scanline += 2;

    if (displ->scanline >= DISPLAY_HEIGHT) {
        field_start(displ);
        return;
    }

    displ->pending |= ((1 << TASK_CURSOR)
                       | (1 << TASK_MEMORY_REFRESH));

    displ->dw_blocked = FALSE;
    displ->fifo_start = displ->fifo_end = 0;
    check_dw_pending(displ);

    if (displ->switch_mode) {
        displ->low_res_latched = displ->low_res;
        displ->wob_latched = displ->wob;
        displ->switch_mode = FALSE;
    }

    displ->dh_intr_cycle =
        INTR_CYCLE(displ->intr_cycle + HBLANK_DURATION);
}

/* Updates the intr_cycle. */
static
void update_intr_cycle(struct display *displ)
{
    int32_t intr_cycles[3];

    intr_cycles[0] = displ->dv_intr_cycle;
    intr_cycles[1] = displ->dh_intr_cycle;
    intr_cycles[2] = displ->dw_intr_cycle;

    displ->intr_cycle = compute_intr_cycle(displ->intr_cycle,
                                           3, intr_cycles);
}

void display_interrupt(struct display *displ)
{
    int has_dv, has_dh, has_dw;

    has_dv = (displ->intr_cycle == displ->dv_intr_cycle);
    has_dh = (displ->intr_cycle == displ->dh_intr_cycle);
    has_dw = (displ->intr_cycle == displ->dw_intr_cycle);

    if (has_dv) dv_interrupt(displ);
    if (has_dh) dh_interrupt(displ);
    if (has_dw) dw_interrupt(displ);

    update_intr_cycle(displ);
}

void display_on_switch_task(struct display *displ, uint8_t task)
{
    if (task != TASK_DISPLAY_HORIZONTAL
        && task != TASK_DISPLAY_VERTICAL
        && task != TASK_CURSOR)
        return;

    /* Automatically blocks task on switch. */
    displ->pending &= ~(1 << task);
}

void display_print_registers(struct display *displ,
                             struct string_buffer *output)
{
    string_buffer_print(output,
                        "SCLIN: %06o     VBLIN: %06o     "
                        "WORD : %06o     EFILD: %o\n",
                        displ->scanline, displ->vblank_scanline,
                        displ->word, displ->even_field ? 1 : 0);

    string_buffer_print(output,
                        "CX%s  : %06o     CX_L : %06o     "
                        "CD%s  : %06o     CD_L : %06o\n",
                        displ->has_cursor_x ? "*" : " ",
                        displ->cursor_x, displ->cursor_x_latched,
                        displ->has_cursor_data ? "*" : " ",
                        displ->cursor_data, displ->cursor_data_latched);

    string_buffer_print(output,
                        "SWT  : %-6o     LRES : %o/%-4o     "
                        "WOB  : %o/%-4o\n",
                        displ->switch_mode ? 1 : 0,
                        displ->low_res ? 1 : 0, displ->low_res_latched ? 1 : 0,
                        displ->wob ? 1 : 0, displ->wob_latched ? 1 : 0);

    string_buffer_print(output,
                        "DWBL : %-6o     DHBL : %-6o     "
                        "PEND : %06o     ICYC : %-10d\n",
                        displ->dw_blocked ? 1 : 0,
                        displ->dh_blocked ? 1 : 0,
                        displ->pending,
                        displ->intr_cycle);

    string_buffer_print(output,
                        "DVIC : %-10d DHIC : %-10d "
                        "DWIC : %-10d\n",
                        displ->dv_intr_cycle,
                        displ->dh_intr_cycle,
                        displ->dw_intr_cycle);
}
