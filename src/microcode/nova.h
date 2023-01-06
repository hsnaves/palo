
#ifndef __MICROCODE_NOVA_H
#define __MICROCODE_NOVA_H

#include <stdint.h>

#include "common/string_buffer.h"

/* Data structures and types. */

/* Structure representing the partially decoded nova instruction. */
struct nova_insn {
    uint16_t address;             /* The address of the instruction
                                   * (including bank number).
                                   */
    uint16_t insn;                /* The instruction itself. */
};

/* The nova instruction decoder. */
struct nova_decoder {
    struct nova_insn ni;          /* The nova instruction.. */
    int error;                    /* Indicates an error. */
};

/* Functions. */

/* Predecodes the nova instruction.
 * The address of the instruction is in `address` (this might include the bank
 * number), and the instruction is in `insns`.
 */
void nova_insn_predecode(struct nova_insn *ni,
                         uint16_t address, uint16_t insn);

/* Decodes the nova instruction from the decoder `ndec` into the
 * output buffer `output`. The details of the instruction are
 * given by `ni`.
 */
void nova_decoder_decode(struct nova_decoder *ndec,
                         const struct nova_insn *ni,
                         struct string_buffer *output);


#endif /* __MICROCODE_NOVA_H */
