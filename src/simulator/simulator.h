
#ifndef __SIMULATOR_SIMULATOR_H
#define __SIMULATOR_SIMULATOR_H

#include <stdint.h>
#include "microcode/microcode.h"
#include "simulator/disk.h"
#include "simulator/display.h"
#include "simulator/ethernet.h"
#include "simulator/keyboard.h"
#include "simulator/mouse.h"
#include "common/utils.h"

/* Data structures and types. */
struct simulator {
    enum system_type sys_type;    /* The alto system type. */
    int error;                    /* The simulator is in an error state. */
    uint16_t *r;                  /* R register file (32 registers). */
    uint16_t *s;                  /* S register file (8 x 32 registers). */

    uint16_t t;                   /* T register. */
    uint16_t l;                   /* L register. */
    uint16_t m;                   /* M register. */
    uint16_t mar;                 /* Memory address register. */
    uint16_t ir;                  /* The instruction register. */
    uint32_t mir;                 /* The micro instruction register. */
    uint16_t mpc;                 /* The MPC corresponding to the MIR. */

    uint8_t ctask;                /* Current task. */
    uint8_t ntask;                /* The next task. */
    int task_switch;              /* If a task switch just happened. */

    int aluC0;                    /* Last carry of ALU when loading L. */
    int skip;                     /* Skip flag. */
    int carry;                    /* Carry flag. */

    int r_changed;                /* A flag indicating that some R register
                                   * changed.
                                   */
    uint16_t modified_rsel;       /* Which register changed. */

    uint16_t rmr;                 /* Reset mode register (for tasks to start
                                   * in either ROM0 or RAM0).
                                   */
    uint16_t cram_addr;           /* Control RAM address. */
    int rdram;                    /* Previous instruction had RDRAM. */
    int wrtram;                   /* Previous instruction had WRTRAM. */
    int swmode;                   /* Previous instruction had SWMODE. */
    int soft_reset;               /* Previous instruction had soft reset. */

    uint16_t *consts;             /* Pointer to the constant rom. */
    uint32_t *microcode;          /* Microcode ROM + RAM. */

    uint16_t *task_mpc;           /* Microcode program counter + bank
                                   * select (1 per task).
                                   */

    int32_t cycle;                /* Current cpu cycle. */
    int32_t *task_cycle;          /* Current task cycles. */
    int32_t intr_cycle;           /* Next cycle when the simulator needs
                                   * to check the controllers for events.
                                   */

    uint16_t *mem;                /* Main memory. */
    uint16_t *xm_banks;           /* Banks for the different tasks. */
    uint8_t *sreg_banks;          /* S register banks for the tasks. */

    uint16_t mem_cycle;           /* A counter to keep track the current
                                   * memory cycle (for reading and writing).
                                   */
    uint8_t mem_task;             /* The task accessing memory. */
    uint16_t mem_low;             /* Latched memory value (1st word). */
    uint16_t mem_high;            /* Latched memory value (2nd word). */
    int mem_status;               /* The status of memory operation. */

    struct disk dsk;              /* The disk controller. */
    struct display displ;         /* The display controller. */
    struct ethernet ether;        /* The ethernet controller. */
    struct keyboard keyb;         /* The keyboard controller. */
    struct mouse mous;            /* The mouse controller. */
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
 * The `sys_type` variable specifies the system type.
 * Returns TRUE on success.
 */
int simulator_create(struct simulator *sim, enum system_type sys_type);

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
                                 const char *filename, uint8_t bank);

/* Resets the simulator. */
void simulator_reset(struct simulator *sim);

/* Reads the memory of the simulator.
 * The address is given by `address`, the current task by `task`,
 * and if this is an extended memory read, `entended_memory` should be
 * set to TRUE.
 * Returns the memory contents.
 */
uint16_t simulator_read(const struct simulator *sim, uint16_t address,
                        uint8_t task, int extended_memory);

/* Writes to memory.
 * The address is given by `address`, the data is given by `data`,
 * the current task by `task`, and if this is an extended memory write,
 * `entended_memory` should be set to TRUE.
 */
void simulator_write(struct simulator *sim, uint16_t address,
                     uint16_t data, uint8_t task, int extended_memory);

/* Performs a simulation step. */
void simulator_step(struct simulator *sim);

/* Updates the input and output state of the simulation.
 * The keyboard input state is given by `keyb` and the mouse input state
 * is given by `mous`. The current pixel data from the display will be
 * copied to `display_data`. If any of these parameter is NULL, the
 * corresponding state will not be copied.
 * Returns TRUE on success.
 */
int simulator_update(struct simulator *sim,
                     const struct keyboard *keyb,
                     const struct mouse *mous,
                     uint8_t *display_data);

/* Disassembles the current microinstruction.
 * The output is written to `output`.
 */
void simulator_disassemble(const struct simulator *sim,
                           struct string_buffer *output);

/* Prints the state of the registers.
 * The output is written to `output`.
 */
void simulator_print_registers(const struct simulator *sim,
                               struct string_buffer *output);

/* Prints the state of the extra registers.
 * The output is written to `output`.
 */
void simulator_print_extra_registers(const struct simulator *sim,
                                     struct string_buffer *output);

/* Disassembles the current NOVA instruction.
 * The output is written to `output`.
 */
void simulator_nova_disassemble(const struct simulator *sim,
                                struct string_buffer *output);

/* Prints the state of the NOVA registers.
 * The output is written to `output`.
 */
void simulator_print_nova_registers(const struct simulator *sim,
                                    struct string_buffer *output);

#endif /* __SIMULATOR_SIMULATOR_H */
