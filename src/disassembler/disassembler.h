
#ifndef __DISASSEMBLER_DISASSEMBLER_H
#define __DISASSEMBLER_DISASSEMBLER_H

#include <stddef.h>
#include <stdint.h>

#include "common/allocator.h"

/* Data structures and types. */

/* Structure to represent a list of addresses. */
struct address_node {
    uint16_t address;             /* The address of the calling microcode. */
    uint16_t next_mask;           /* Which bits were modified to reach
                                   * the current microcode.
                                   */
    uint32_t following_next_mask; /* Which bits can be modified in the
                                   * current microcode.
                                   */
    struct address_node *next;    /* The next node in the list. */
};

/* Structure to represent what is known about an instruction. */
struct instruction {
    uint16_t task_mask;           /* Set of tasks that are known
                                   * to run this instruction.
                                   */
    uint32_t details;             /* The details about the instruction. */
    struct address_node *callers; /* List of addresses that
                                   * can jump to this instruction.
                                   */
};

/* Structure to represent the disassembler. */
struct disassembler {
    struct allocator oalloc;      /* The object allocator. */

    uint16_t *consts;             /* The value of the constants. */
    uint32_t *microcode;          /* The microcode. */

    struct instruction *insns;    /* The instruction details. */

    uint16_t *stack;              /* Used as an address stack for
                                   * propagation.
                                   */
    uint16_t stack_top;           /* The top of the stack. */

    struct address_node *free_nodes; /* List of free address nodes. */
};

/* Functions. */

/* Initializes the disassembler variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void disassembler_initvar(struct disassembler *dis);

/* Destroys the disassembler object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void disassembler_destroy(struct disassembler *dis);

/* Creates a new disassembler object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int disassembler_create(struct disassembler *dis);

/* Loads the constant rom from a file.
 * The filename with the constants is defined by parameter `filename`.
 * It assumes the file is in little-endian format.
 * Returns TRUE on success.
 */
int disassembler_load_constant_rom(struct disassembler *dis,
                                   const char *filename);

/* Loads the microcode rom from a file.
 * The filename with the microcode is defined by parameter `filename`.
 * It assumes the file is in little-endian format.
 * Returns TRUE on success.
 */
int disassembler_load_microcode_rom(struct disassembler *dis,
                                    const char *filename);

/* Tries to figure out the addresses of each task.
 * Returns TRUE on sucess.
 */
int disassembler_find_task_addresses(struct disassembler *dis);

/* Disassembles one microinstruction.
 * The address to disassemble is given by `address`, and the current
 * task is given by `task`. The output is written to `output`,
 * which is a buffer of size `output_size`.
 * Returns TRUE on success.
 */
int disassembler_disassemble(struct disassembler *dis,
                             uint16_t address, uint8_t task,
                             char *output, size_t output_size);

#endif /* __DISASSEMBLER_DISASSEMBLER_H */
