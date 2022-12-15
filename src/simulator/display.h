
#ifndef __SIMULATOR_DISPLAY_H
#define __SIMULATOR_DISPLAY_H

#include <stdint.h>

/* Data structures and types. */
struct display {
    uint16_t *data_buffer;        /* The data bufferimplementing
                                   * the pixel FIFO.
                                   */
};

/* Functions. */

/* Initializes the display variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void display_initvar(struct display *d);

/* Destroys the display object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void display_destroy(struct display *d);

/* Creates a new display object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int display_create(struct display *d);


#endif /* __SIMULATOR_DISPLAY_H */
