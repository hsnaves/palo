
#ifndef __SIMULATOR_DISPLAY_H
#define __SIMULATOR_DISPLAY_H

#include <stdint.h>

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
    int scanline;                 /* The current scal line. */

    uint16_t cursor_x;            /* Cursor X position. */
    uint16_t cursor_data;         /* The cursor data. */

    uint32_t intr_cycle;          /* Cycle of the next interrupt. */
    int dw_pending;               /* Display word task pending. */
    int dh_pending;               /* Display horizontal task pending. */
    int dv_pending;               /* Display vertical task pending. */
    int cur_pending;              /* Cursor task pending. */
    int mr_pending;               /* Memory refersh task pending. */
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
 */
void display_load_ddr(struct display *displ, uint16_t bus);

/* Sets the display mode.
 * The bus value is given by the parameter `bus`.
 */
void display_set_mode(struct display *displ, uint16_t bus);

/* Processes the display interrupts. */
void display_interrupt(struct display *displ);

#endif /* __SIMULATOR_DISPLAY_H */
