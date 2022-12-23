
#include <stdint.h>
#include <string.h>

#include "simulator/keyboard.h"
#include "common/utils.h"

/* Static tables. */

static const struct {
    uint16_t word_index;
    uint16_t mask;
} KEY_MAP[] = {
    [AK_NONE]        = { 0, 0x0000 },

    [AK_5]           = { 0, 0x8000 },
    [AK_4]           = { 0, 0x4000 },
    [AK_6]           = { 0, 0x2000 },
    [AK_E]           = { 0, 0x1000 },
    [AK_7]           = { 0, 0x0800 },
    [AK_D]           = { 0, 0x0400 },
    [AK_U]           = { 0, 0x0200 },
    [AK_V]           = { 0, 0x0100 },
    [AK_0]           = { 0, 0x0080 },
    [AK_K]           = { 0, 0x0040 },
    [AK_MINUS]       = { 0, 0x0020 },
    [AK_P]           = { 0, 0x0010 },
    [AK_FSLASH]      = { 0, 0x0008 },
    [AK_BSLASH]      = { 0, 0x0004 },
    [AK_LF]          = { 0, 0x0002 },
    [AK_BS]          = { 0, 0x0001 },

    [AK_3]           = { 1, 0x8000 },
    [AK_2]           = { 1, 0x4000 },
    [AK_W]           = { 1, 0x2000 },
    [AK_Q]           = { 1, 0x1000 },
    [AK_S]           = { 1, 0x0800 },
    [AK_A]           = { 1, 0x0400 },
    [AK_9]           = { 1, 0x0200 },
    [AK_I]           = { 1, 0x0100 },
    [AK_X]           = { 1, 0x0080 },
    [AK_O]           = { 1, 0x0040 },
    [AK_L]           = { 1, 0x0020 },
    [AK_COMMA]       = { 1, 0x0010 },
    [AK_QUOTE]       = { 1, 0x0008 },
    [AK_RBRACKET]    = { 1, 0x0004 },
    [AK_BLANKMIDDLE] = { 1, 0x0002 },
    [AK_BLANKTOP]    = { 1, 0x0001 },

    [AK_1]           = { 2, 0x8000 },
    [AK_ESC]         = { 2, 0x4000 },
    [AK_TAB]         = { 2, 0x2000 },
    [AK_F]           = { 2, 0x1000 },
    [AK_CTRL]        = { 2, 0x0800 },
    [AK_C]           = { 2, 0x0400 },
    [AK_J]           = { 2, 0x0200 },
    [AK_B]           = { 2, 0x0100 },
    [AK_Z]           = { 2, 0x0080 },
    [AK_LSHIFT]      = { 2, 0x0040 },
    [AK_PERIOD]      = { 2, 0x0020 },
    [AK_SEMICOLON]   = { 2, 0x0010 },
    [AK_RETURN]      = { 2, 0x0008 },
    [AK_ARROW]       = { 2, 0x0004 },
    [AK_DEL]         = { 2, 0x0002 },

    [AK_R]           = { 3, 0x8000 },
    [AK_T]           = { 3, 0x4000 },
    [AK_G]           = { 3, 0x2000 },
    [AK_Y]           = { 3, 0x1000 },
    [AK_H]           = { 3, 0x0800 },
    [AK_8]           = { 3, 0x0400 },
    [AK_N]           = { 3, 0x0200 },
    [AK_M]           = { 3, 0x0100 },
    [AK_LOCK]        = { 3, 0x0080 },
    [AK_SPACE]       = { 3, 0x0040 },
    [AK_LBRACKET]    = { 3, 0x0020 },
    [AK_PLUS]        = { 3, 0x0010 },
    [AK_RSHIFT]      = { 3, 0x0008 },
    [AK_BLANKBOTTOM] = { 3, 0x0004 },
};

/* Functions. */

void keyboard_initvar(struct keyboard *keyb)
{
    /* Nothing to do here. */
    UNUSED(keyb);
}

void keyboard_destroy(struct keyboard *keyb)
{
    /* Nothing to do here. */
    UNUSED(keyb);
}

int keyboard_create(struct keyboard *keyb)
{
    keyboard_initvar(keyb);
    keyboard_reset(keyb);
    return TRUE;
}

void keyboard_update_from(struct keyboard *keyb,
                          const struct keyboard *other)
{
    memcpy(keyb->keys, other->keys, sizeof(keyb->keys));
}

void keyboard_reset(struct keyboard *keyb)
{
    memset(keyb->keys, 0, sizeof(keyb->keys));
}

uint16_t keyboard_read(const struct keyboard *keyb, uint16_t address)
{
    if (address < KEYBOARD_BASE || address >= KEYBOARD_END)
        return 0;

    /* Keyword is inverted. */
    return ~(keyb->keys[address - KEYBOARD_BASE]);
}

void keyboard_press_key(struct keyboard *keyb, enum alto_key key)
{
    uint16_t word_index;
    uint16_t mask;

    if (key < AK_NONE || key >= AK_LAST_KEY)
        return;

    word_index = KEY_MAP[key].word_index;
    mask = KEY_MAP[key].mask;

    keyb->keys[word_index] |= mask;
}

void keyboard_release_key(struct keyboard *keyb, enum alto_key key)
{
    uint16_t word_index;
    uint16_t mask;

    if (key < AK_NONE || key >= AK_LAST_KEY)
        return;

    word_index = KEY_MAP[key].word_index;
    mask = KEY_MAP[key].mask;

    keyb->keys[word_index] &= (~mask);
}
