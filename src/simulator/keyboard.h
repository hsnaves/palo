
#ifndef __SIMULATOR_KEYBOARD_H
#define __SIMULATOR_KEYBOARD_H

#include <stdint.h>

/* Data structures and types. */
struct keyboard {
    uint16_t keys[4];
};

/* Functions. */

/* Initializes the keyboard variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void keyboard_initvar(struct keyboard *k);

/* Destroys the keyboard object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void keyboard_destroy(struct keyboard *k);

/* Creates a new keyboard object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int keyboard_create(struct keyboard *k);


#endif /* __SIMULATOR_KEYBOARD_H */
