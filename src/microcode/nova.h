
#ifndef __MICROCODE_NOVA_H
#define __MICROCODE_NOVA_H

#include <stdint.h>

#include "microcode/microcode.h"
#include "common/string_buffer.h"

/* Data structures and types. */

/* Structure representing the partially decoded nova instruction. */
struct nova_insn {
    uint16_t address;             /* The address of the instruction
                                   * (including bank number).
                                   */
    uint16_t insn;                /* The instruction itself. */
};

/* Functions. */

/* Predecodes the nova instruction.
 * The address of the instruction is in `address` (this might include the bank
 * number), and the instruction is in `insns`.
 */
void nova_insn_predecode(struct nova_insn *ni,
                         uint16_t address, uint16_t insn);

/* Decodes the nova instruction `ni` using the decoder `dec`. */
void nova_insn_decode(struct decoder *dec,
                      const struct nova_insn *ni);


#endif /* __MICROCODE_NOVA_H */
