
#ifndef __SIMULATOR_DISPLAY_H
#define __SIMULATOR_DISPLAY_H

#include <stdint.h>

#include "common/serdes.h"
#include "common/utils.h"

/* Constants. */
#define DISPLAY_WIDTH                    606
#define DISPLAY_HEIGHT                   808
#define DISPLAY_STRIDE                   608
#define DISPLAY_DATA_SIZE (DISPLAY_STRIDE * DISPLAY_HEIGHT)

/* Data structures and types. */

/* The display controller structure used by the simulator. */
struct display {
    uint8_t *display_data;        /* The display pixels.
                                   * Here we use bytes instead of
                                   * bits because most graphics libraries
                                   * do not suport 1BPP pixel formats.
                                   */
    uint16_t *fifo;               /* The data buffer implementing
                                   * the pixel FIFO.
                                   */
    uint8_t fifo_start, fifo_end; /* To control the FIFO. */

    int even_field;               /* If this is an even or odd field. */
    uint16_t scanline;            /* The current scanline. */
    uint16_t vblank_scanline;     /* Scanline in vertical blank period. */
    uint16_t word;                /* The current word in the scanline. */

    uint16_t cursor_x;            /* Cursor X position. */
    uint16_t cursor_x_latched;    /* Latched position for scanline. */
    int has_cursor_x;             /* X data latched. */
    uint16_t cursor_data;         /* The cursor data. */
    uint16_t cursor_data_latched; /* Latched data for scanline. */
    int has_cursor_data;          /* Cursor data latched. */

    int switch_mode;              /* Pending switch mode requested. */
    int low_res;                  /* Low resolution mode. */
    int low_res_latched;          /* The latched mode for scanline. */
    int wob;                      /* White on black. */
    int wob_latched;              /* Latched wob for scanline. */

    int dw_blocked;               /* Display word blocked itself. */
    int dh_blocked;               /* Display horizontal blocked itself. */

    int32_t intr_cycle;           /* Cycle of the next interrupt. */
    int32_t dv_intr_cycle;        /* Display vertical interrupt cycle. */
    int32_t dh_intr_cycle;        /* Display horizontal interrupt cycle. */
    int32_t dw_intr_cycle;        /* Display word interrupt cycle. */
    uint16_t pending;             /* The task pending mask. */
};

/* Functions. */

/* Initializes the display variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void display_initvar(struct display *displ);

/* Destroys the display object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void display_destroy(struct display *displ);

/* Creates a new display object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int display_create(struct display *displ);

/* Resets the display controller. */
void display_reset(struct display *displ);

/* Loads a word into the data display register.
 * The word from the bus to load is given by `bus`.
 * Returns TRUE on success.
 */
int display_load_ddr(struct display *displ, uint16_t bus);

/* Loads a word into the cursor X position register.
 * The word from the bus to load is given by `bus`.
 */
void display_load_xpreg(struct display *displ, uint16_t bus);

/* Loads a word into the cursor data register.
 * The word from the bus to load is given by `bus`.
 */
void display_load_csr(struct display *displ, uint16_t bus);

/* Checks if it is an even field
 * (each frame consists of 2 fields, even and odd).
 * Returns the bits to be modified in the NEXT part of the
 * following microinstruction.
 */
uint16_t display_even_field(struct display *displ);

/* Sets the display mode.
 * The bus value is given by the parameter `bus`.
 * Returns the bits to be modified in the NEXT part of the
 * following microinstruction.
 */
uint16_t display_set_mode(struct display *displ, uint16_t bus);

/* Processes a BLOCK instruction.
 * The task to be blocked is in the parameter `task`.
 */
void display_block_task(struct display *displ, uint8_t task);

/* Processes the display interrupts. */
void display_interrupt(struct display *displ);

/* Callback for when the simulation switches to a display task.
 * The new task is given by `task`.
 */
void display_on_switch_task(struct display *displ, uint8_t task);

/* Prints the state of the registers.
 * The output is written to `output`.
 */
void display_print_registers(struct display *displ,
                             struct string_buffer *output);

/* Serializes the display object to `sd`. */
void display_serialize(const struct display *displ, struct serdes *sd);

/* Deserializes the display object from `sd`. */
void display_deserialize(struct display *displ, struct serdes *sd);

#endif /* __SIMULATOR_DISPLAY_H */
