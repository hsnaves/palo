
#ifndef __SIMULATOR_MOUSE_H
#define __SIMULATOR_MOUSE_H

#include <stdint.h>

/* Constants. */
#define MOUSE_BASE            0xFE18
#define MOUSE_END             0xFE1C

/* Data structures and types. */

/* Possible Alto mouse buttons. */
enum alto_button {
    AB_NONE = 0,
    AB_BTN_MIDDLE,
    AB_BTN_RIGHT,
    AB_BTN_LEFT,
    AB_KEYSET0,
    AB_KEYSET1,
    AB_KEYSET2,
    AB_KEYSET3,
    AB_KEYSET4,
    AB_LAST_BUTTON,
};

/* The mouse (and keyset) controller for the simulator. */
struct mouse {
    uint16_t buttons;             /* The bit mask of pressed buttons. */
    int dx, dy;                   /* Mouse movement. */
    int dir_x;                    /* Current movement direction in X axis. */
};

/* Functions. */

/* Initializes the mouse variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void mouse_initvar(struct mouse *mous);

/* Destroys the mouse object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void mouse_destroy(struct mouse *mous);

/* Creates a new mouse object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int mouse_create(struct mouse *mous);

/* Updates the state from other mouse object. */
void mouse_update_from(struct mouse *mous, const struct mouse *other);

/* Resets the state of the mouse. */
void mouse_reset(struct mouse *mous);

/* Reads some data from the mouse controller.
 * The address to read is in `address`.
 * Returns the word from the mouse.
 */
uint16_t mouse_read(const struct mouse *mous, uint16_t address);

/* Get the bits for the "<-MOUSE" bus source.
 * Returns the mouse bits.
 */
uint16_t mouse_poll_bits(struct mouse *mous);

/* Presses a button.
 * The button to press is given by `btn`
 */
void mouse_press_button(struct mouse *mous, enum alto_button btn);

/* Releases a button.
 * The button to release is given by `btn`
 */
void mouse_release_button(struct mouse *mous, enum alto_button btn);

/* Moves the mouse.
 * The amount to move in the X (and Y) direction is given by
 * `dx` (and `dy`, respectively).
 */
void mouse_move(struct mouse *mous, int dx, int dy);

/* Clears the pending mouse movements. */
void mouse_clear_movement(struct mouse *mous);


#endif /* __SIMULATOR_MOUSE_H */
