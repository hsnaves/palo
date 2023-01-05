
#ifndef __SIMULATOR_KEYBOARD_H
#define __SIMULATOR_KEYBOARD_H

#include <stdint.h>

#include "common/serdes.h"

/* Constants. */
#define KEYBOARD_BASE                 0xFE1C
#define KEYBOARD_END                  0xFE20

/* Data structures and types. */

/* Possible Alto keys. */
enum alto_key {
    AK_NONE = 0,
    AK_A,
    AK_B,
    AK_C,
    AK_D,
    AK_E,
    AK_F,
    AK_G,
    AK_H,
    AK_I,
    AK_J,
    AK_K,
    AK_L,
    AK_M,
    AK_N,
    AK_O,
    AK_P,
    AK_Q,
    AK_R,
    AK_S,
    AK_T,
    AK_U,
    AK_V,
    AK_W,
    AK_X,
    AK_Y,
    AK_Z,
    AK_0,
    AK_1,
    AK_2,
    AK_3,
    AK_4,
    AK_5,
    AK_6,
    AK_7,
    AK_8,
    AK_9,
    AK_SPACE,
    AK_PLUS,
    AK_MINUS,
    AK_COMMA,
    AK_PERIOD,
    AK_SEMICOLON,
    AK_QUOTE,
    AK_LBRACKET,
    AK_RBRACKET,
    AK_FSLASH,
    AK_BSLASH,
    AK_ARROW,
    AK_LOCK,
    AK_LSHIFT,
    AK_RSHIFT,
    AK_LF,
    AK_BS,
    AK_DEL,
    AK_ESC,
    AK_TAB,
    AK_CTRL,
    AK_RETURN,
    AK_BLANKTOP,
    AK_BLANKMIDDLE,
    AK_BLANKBOTTOM,
    AK_LAST_KEY,   /* Not a real key, used as sentinel value. */
};

/* The keyboard controller for the simulator. */
struct keyboard {
    uint16_t keys[4];             /* The bit mask of pressed keys. */
};

/* Functions. */

/* Initializes the keyboard variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void keyboard_initvar(struct keyboard *keyb);

/* Destroys the keyboard object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void keyboard_destroy(struct keyboard *keyb);

/* Creates a new keyboard object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int keyboard_create(struct keyboard *keyb);

/* Updates the state from other keyboard object. */
void keyboard_update_from(struct keyboard *keyb,
                          const struct keyboard *other);

/* Resets the state of the keyboard. */
void keyboard_reset(struct keyboard *keyb);

/* Reads some data from the keyboard controller.
 * The address to read is in `address`.
 * Returns the word from the keyboard.
 */
uint16_t keyboard_read(const struct keyboard *keyb, uint16_t address);

/* Presses a key.
 * The key to press is given by `key`
 */
void keyboard_press_key(struct keyboard *keyb, enum alto_key key);

/* Releases a key.
 * The key to release is given by `key`
 */
void keyboard_release_key(struct keyboard *keyb, enum alto_key key);

/* Serializes the keyboard object to `sd`. */
void keyboard_serialize(const struct keyboard *keyb, struct serdes *sd);

/* Deserializes the keyboard object from `sd`. */
void keyboard_deserialize(struct keyboard *keyb, struct serdes *sd);


#endif /* __SIMULATOR_KEYBOARD_H */
