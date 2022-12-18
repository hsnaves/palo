
#include <stdint.h>

#include "simulator/mouse.h"
#include "common/utils.h"

/* Constants. */
#define MOVE_NOCHANGE        0x001
#define MOVE_DOWN            0x002
#define MOVE_UP              0x004
#define MOVE_LEFT            0x008
#define MOVE_DOWN_LEFT       0x010
#define MOVE_UP_LEFT         0x020
#define MOVE_RIGHT           0x040
#define MOVE_DOWN_RIGHT      0x080
#define MOVE_UP_RIGHT        0x100

/* Static tables. */

static const uint16_t BUTTON_MAP[] = {
    [AB_NONE]           = 0x0000,
    [AB_BTN_MIDDLE]     = 0x0001,
    [AB_BTN_RIGHT]      = 0x0002,
    [AB_BTN_LEFT]       = 0x0004,
    [AB_KEYSET0]        = 0x0080,
    [AB_KEYSET1]        = 0x0040,
    [AB_KEYSET2]        = 0x0020,
    [AB_KEYSET3]        = 0x0010,
    [AB_KEYSET4]        = 0x0008,
};


/* Functions. */

void mouse_initvar(struct mouse *mous)
{
    /* Nothing to do here. */
}

void mouse_destroy(struct mouse *mous)
{
    /* Nothing to do here. */
}

int mouse_create(struct mouse *mous)
{
    mouse_initvar(mous);
    mouse_reset(mous);
    return TRUE;
}

void mouse_reset(struct mouse *mous)
{
    mous->buttons = 0;
    mous->x = mous->y = 0;
    mous->target_x = mous->target_y = 0;
    mous->dir_x = FALSE;
}

uint16_t mouse_read(const struct mouse *mous, uint16_t address)
{
    return mous->buttons;
}

uint16_t mouse_poll_bits(struct mouse *mous)
{
    uint16_t bits;
    int dx, dy;

    dx = mous->target_x - mous->x;
    dy = mous->target_y - mous->y;

    bits = 0;
    if (dx == 0 && dy == 0) return bits;

    /* Simulate the mouse movement. */
    if (mous->dir_x) {
        if (dx > 0)
            bits |= MOVE_RIGHT;
        else if (dx < 0)
            bits |= MOVE_LEFT;
    } else {
        if (dy > 0)
            bits |= MOVE_UP;
        else if (dy < 0)
            bits |= MOVE_DOWN;
    }

    mous->dir_x = !(mous->dir_x);
    return bits;
}

void mouse_press_button(struct mouse *mous, enum alto_button btn)
{
    uint16_t mask;

    if (btn < AB_NONE || btn >= AB_LAST_BUTTON)
        return;

    mask = BUTTON_MAP[btn];
    mous->buttons |= mask;
}

void mouse_release_button(struct mouse *mous, enum alto_button btn)
{
    uint16_t mask;

    if (btn < AB_NONE || btn >= AB_LAST_BUTTON)
        return;

    mask = BUTTON_MAP[btn];
    mous->buttons &= (~mask);
}

void mouse_move(struct mouse *mous, int dx, int dy)
{
    mous->target_x += dx;
    mous->target_y += dy;
}
