
#ifndef __SIMULATOR_SIMULATOR_H
#define __SIMULATOR_SIMULATOR_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */
struct simulator {
    uint16_t t;           /* T register */
    uint16_t l;           /* L register */
    uint16_t m;           /* M register */
    uint16_t ir;          /* IR register */

    uint16_t *r;          /* R register file (32 registers) */
    uint16_t *s;          /* S register file (8 x 32 registers) */

    int aluC0;            /* Last carry of ALU when loading L. */
    uint16_t rmr;         /* Reset mode register
                           * (for tasks to start in either ROM0 or RAM0).
                           */

    uint16_t *crom;       /* Pointer to the constant rom. */
    uint32_t *mrom;       /* Microcode rom. */
};

/* Functions. */

/* Initializes the simulator variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void simulator_initvar(struct simulator *sim);

/* Destroys the simulator object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void simulator_destroy(struct simulator *sim);

/* Creates a new simulator object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int simulator_create(struct simulator *sim);

/* Resets the simulator state. */
void simulator_reset(struct simulator *sim);

#endif /* __SIMULATOR_SIMULATOR_H */
