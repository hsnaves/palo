
#include <stdint.h>

#include "simulator/keyboard.h"
#include "common/utils.h"

/* Functions. */

void keyboard_initvar(struct keyboard *keyb)
{
}

void keyboard_destroy(struct keyboard *keyb)
{
}

int keyboard_create(struct keyboard *keyb)
{
    keyboard_initvar(keyb);
    return TRUE;
}

