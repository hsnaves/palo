
#ifndef __SIMULATOR_SIMULATOR_H
#define __SIMULATOR_SIMULATOR_H

#include <stdint.h>

/* Data structures and types. */
struct simulator {
    uint16_t *r;          /* R register file (32 registers). */
    uint16_t *s;          /* S register file (8 x 32 registers). */

    uint16_t t;           /* T register. */
    uint16_t l;           /* L register. */
    uint16_t m;           /* M register. */
    uint16_t mar;         /* Memory address register. */
    uint16_t ir;          /* The instruction register. */
    uint32_t mir;         /* The micro instruction register. */

    uint8_t ctask;        /* Current task. */
    uint8_t ntask;        /* The next task. */
    uint16_t pending;     /* The bit mask of pending tasks. */

    int aluC0;            /* Last carry of ALU when loading L. */
    int skip;             /* Skip flag. */
    int carry;            /* Carry flag. */
    int dns;              /* Do NOVA style shifts. */
    uint16_t rmr;         /* Reset mode register
                           * (for tasks to start in either ROM0 or RAM0).
                           */

    uint16_t *consts;     /* Pointer to the constant rom. */
    uint32_t *microcode;  /* Microcode ROM + RAM. */

    uint16_t *mpc;        /* Microcode program counter (1 per task). */

    uint16_t *mem;        /* Main memory. */
    uint16_t *xm_banks;   /* Banks for the different tasks. */
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

/* Loads the constant rom from a file.
 * The filename with the constants is defined by parameter `filename`.
 * It assumes the file is in little-endian format.
 * Returns TRUE on success.
 */
int simulator_load_constant_rom(struct simulator *sim,
                                const char *filename);

/* Loads the microcode rom from a file.
 * The filename with the microcode is defined by parameter `filename`.
 * The bank number is specified by the parameter `bank`.
 * It assumes the file is in little-endian format.
 * Returns TRUE on success.
 */
int simulator_load_microcode_rom(struct simulator *sim,
                                 const char *filename,
                                 unsigned int bank);

/* Resets the simulator. */
void simulator_reset(struct simulator *sim);

/* Performs a simulation step. */
void simulator_step(struct simulator *sim);

#endif /* __SIMULATOR_SIMULATOR_H */
