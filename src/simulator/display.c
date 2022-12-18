
#include <stdint.h>
#include <stdlib.h>

#include "simulator/display.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define DISPLAY_WIDTH      606
#define DISPLAY_HEIGHT     808
#define FIFO_SIZE           16
#define DISPLAY_DATA_SIZE  (DISPLAY_WIDTH * DISPLAY_HEIGHT)

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
    displ->even_field = TRUE;
    displ->scanline = 0;
    displ->cursor_x = 0;
    displ->cursor_data = 0;

    displ->intr_cycle = 0xFFFFFFFFU;
    displ->dw_intr_cycle = 0xFFFFFFFFU;
    displ->dh_intr_cycle = 0xFFFFFFFFU;
    displ->dv_intr_cycle = 0xFFFFFFFFU;
    displ->pending = 0;
}

void display_load_ddr(struct display *displ, uint16_t bus)
{
    /* TODO: Implement this. */
}

void display_load_xpreg(struct display *displ, uint16_t bus)
{
    /* TODO: Implement this. */
    displ->cursor_x = bus;
}

void display_load_csr(struct display *displ, uint16_t bus)
{
    /* TODO: Implement this. */
    displ->cursor_data = bus;
}

uint16_t display_even_field(struct display *displ)
{
    return (displ->even_field) ? 1 : 0;
}

uint16_t display_set_mode(struct display *displ, uint16_t bus)
{
    /* TODO: Implement this. */
    return (bus & 0x8000) ? 1 : 0;
}

/* Updates the intr_cycle. */
static
void update_intr_cycle(struct display *displ)
{
    uint32_t tmp, diff;

    diff = displ->dw_intr_cycle;
    tmp = displ->dh_intr_cycle;
    if (diff > tmp) diff = tmp;
    tmp = displ->dv_intr_cycle;
    if (diff > tmp) diff = tmp;

    displ->intr_cycle += diff;
}

void display_block_task(struct display *displ, uint8_t task)
{
    /* TODO: Implement this. */
    displ->pending &= ~(1 << task);
}

void display_interrupt(struct display *displ)
{
    /* TODO: Implement this. */

    update_intr_cycle(displ);
}
