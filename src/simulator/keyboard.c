
#include <stdint.h>

#include "simulator/keyboard.h"
#include "common/utils.h"

/* Functions. */

void keyboard_initvar(struct keyboard *k)
{
}

void keyboard_destroy(struct keyboard *k)
{
}

int keyboard_create(struct keyboard *k)
{
    keyboard_initvar(k);
    return TRUE;
}

