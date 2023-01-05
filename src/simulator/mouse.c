
#include <stdint.h>

#include "simulator/mouse.h"
#include "common/serdes.h"
#include "common/utils.h"

/* Constants. */
#define MOVE_NOCHANGE                    0x0
#define MOVE_DOWN                        0x1
#define MOVE_UP                          0x2
#define MOVE_LEFT                        0x3
#define MOVE_RIGHT                       0x6

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
    UNUSED(mous);
}

void mouse_destroy(struct mouse *mous)
{
    /* Nothing to do here. */
    UNUSED(mous);
}

int mouse_create(struct mouse *mous)
{
    mouse_initvar(mous);
    mouse_reset(mous);
    return TRUE;
}

void mouse_update_from(struct mouse *mous, const struct mouse *other)
{
    mous->buttons = other->buttons;
    mous->dx += other->dx;
    mous->dy += other->dy;
}

void mouse_reset(struct mouse *mous)
{
    mous->buttons = 0;
    mous->dx = mous->dy = 0;
    mous->dir_x = FALSE;
}

uint16_t mouse_read(const struct mouse *mous, uint16_t address)
{
    UNUSED(address);
    return mous->buttons;
}

uint16_t mouse_poll_bits(struct mouse *mous)
{
    uint16_t bits;


    bits = MOVE_NOCHANGE;
    if (mous->dx == 0 && mous->dy == 0) return bits;

    /* Simulate the mouse movement. */
    if (mous->dir_x) {
        if (mous->dx > 0) {
            bits = MOVE_RIGHT;
            mous->dx--;
        } else if (mous->dx < 0) {
            bits = MOVE_LEFT;
            mous->dx++;
        }
    } else {
        if (mous->dy > 0) {
            bits = MOVE_UP;
            mous->dy--;
        } else if (mous->dy < 0) {
            bits = MOVE_DOWN;
            mous->dy++;
        }
    }

    mous->dir_x = !(mous->dir_x);

    /* Rest of the bits are 1. */
    return (0xFFF0 | bits);
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

void mouse_move(struct mouse *mous, int16_t dx, int16_t dy)
{
    mous->dx += dx;
    mous->dy += dy;
}

void mouse_clear_movement(struct mouse *mous)
{
    mous->dx = 0;
    mous->dy = 0;
}

void mouse_serialize(const struct mouse *mous, struct serdes *sd)
{
    serdes_put16(sd, mous->buttons);
    serdes_put16(sd, mous->dx);
    serdes_put16(sd, mous->dy);
    serdes_put_bool(sd, mous->dir_x);
}

void mouse_deserialize(struct mouse *mous, struct serdes *sd)
{
    mous->buttons = serdes_get16(sd);
    mous->dx = serdes_get16(sd);
    mous->dy = serdes_get16(sd);
    mous->dir_x = serdes_get_bool(sd);
}
