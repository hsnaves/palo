
#ifndef __SIMULATOR_MOUSE_H
#define __SIMULATOR_MOUSE_H

#include <stdint.h>

/* Data structures and types. */
struct mouse {
    uint16_t buttons;
    uint16_t bits;
};

/* Functions. */

/* Initializes the mouse variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void mouse_initvar(struct mouse *m);

/* Destroys the mouse object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void mouse_destroy(struct mouse *m);

/* Creates a new mouse object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int mouse_create(struct mouse *m);


/* Get the bits for the "<-MOUSE" bus source.
 * Returns the mouse bits.
 */
uint16_t mouse_poll_bits(struct mouse *m);

#endif /* __SIMULATOR_MOUSE_H */
