
#include <stdint.h>
#include <stdlib.h>

#include "simulator/display.h"
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
    displ->dw_pending = FALSE;
    displ->dh_pending = FALSE;
    displ->dv_pending = FALSE;
    displ->cur_pending = FALSE;
    displ->mr_pending = FALSE;
}

void display_load_ddr(struct display *displ, uint16_t bus)
{
    /* TODO: Implement this. */
}

void display_set_mode(struct display *displ, uint16_t bus)
{
    /* TODO: Implement this. */
}

void display_interrupt(struct display *displ)
{
}
