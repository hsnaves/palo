
#ifndef __DISASSEMBLER_DISASSEMBLER_H
#define __DISASSEMBLER_DISASSEMBLER_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */

/* Structure to represent the disassembler. */
struct disassembler {
    uint16_t *consts;             /* The value of the constants. */
    uint32_t *microcode;          /* The microcode. */

    uint16_t *task_mask;          /* The possible tasks that
                                   * can execute this microcode.
                                   */
    uint16_t *next_mask;          /* Possible bits that can change
                                   * in the next field of the
                                   * microcode instruction.
                                   */
    uint16_t *hint_mask;          /* To help it when F2=BUS. */

    uint16_t stack_top;           /* The top of the stack. */
    uint16_t *stack;              /* Used as an address stack for
                                   * propagation.
                                   */
    uint8_t *mark;                /* To mark addresses in the stack. */
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

/* Tries to figure out the addresses of each task. */
void disassembler_find_task_addresses(struct disassembler *dis);

#endif /* __DISASSEMBLER_DISASSEMBLER_H */
