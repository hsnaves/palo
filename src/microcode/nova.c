#include <stddef.h>
#include <stdint.h>

#include "microcode/nova.h"
#include "microcode/microcode.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Functions. */

void nova_insn_predecode(struct nova_insn *ni,
                         uint16_t address, uint16_t insn)
{
    ni->address = address;
    ni->insn = insn;
}

/* Decodes a M-group class instruction.
 * Output is appended to `output`.
 */
static
void decode_mgroup(struct decoder *dec,
                   const struct nova_insn *ni)
{
    struct string_buffer *output;
    uint16_t dest_ac;
    uint16_t disp;
    uint16_t addressing;
    uint16_t func;
    int indirect;

    output = dec->output;
    disp = (ni->insn & 0xFF);
    /* Sign extends. */
    if (disp & 0x80) disp |= 0xFF00;

    addressing = (ni->insn >> 8) & 3;
    indirect = (ni->insn >> 10) & 1;
    dest_ac = (ni->insn >> 11) & 3;
    func = (ni->insn >> 13) & 3;

    if (func == 1) {
        string_buffer_print(output, "LDA ");
    } else {
        string_buffer_print(output, "STA ");
    }

    string_buffer_print(output, "%o ", dest_ac);

    if (indirect) {
        string_buffer_print(output, "@");
    }

    switch (addressing) {
    case 0: /* Page zero. */
        decode_value(dec->vdec, DECODE_VALUE, disp & 0xFF);
        break;
    case 1: /* PC relative. */
        string_buffer_print(output, ".+");
        decode_value(dec->vdec, DECODE_VALUE, disp);
        break;
    case 2: /* AC2 relative. */
        decode_value(dec->vdec, DECODE_VALUE, disp);
        string_buffer_print(output, ",2");
        break;
    case 3: /* AC3 relative. */
        decode_value(dec->vdec, DECODE_VALUE, disp);
        string_buffer_print(output, ",3");
        break;
    }
}

/* Decodes a J-group class instruction.
 * Output is appended to `output`.
 */
static
void decode_jgroup(struct decoder *dec,
                   const struct nova_insn *ni)
{
    struct string_buffer *output;
    uint16_t disp;
    uint16_t addressing;
    uint16_t jfunc;
    int indirect;

    output = dec->output;
    disp = (ni->insn & 0xFF);
    /* Sign extends. */
    if (disp & 0x80) disp |= 0xFF00;

    addressing = (ni->insn >> 8) & 3;
    indirect = (ni->insn >> 10) & 1;
    jfunc = (ni->insn >> 11) & 3;

    switch (jfunc) {
    case 0:
        string_buffer_print(output, "JMP ");
        break;
    case 1:
        string_buffer_print(output, "JSR ");
        break;
    case 2:
        string_buffer_print(output, "ISZ ");
        break;
    case 3:
        string_buffer_print(output, "DSZ ");
        break;
    }

    if (indirect) {
        string_buffer_print(output, "@");
    }

    switch (addressing) {
    case 0: /* Page zero. */
        decode_value(dec->vdec, DECODE_VALUE, disp & 0xFF);
        break;
    case 1: /* PC relative. */
        string_buffer_print(output, ".+");
        decode_value(dec->vdec, DECODE_VALUE, disp);
        break;
    case 2: /* AC2 relative. */
        decode_value(dec->vdec, DECODE_VALUE, disp);
        string_buffer_print(output, ",2");
        break;
    case 3: /* AC3 relative. */
        decode_value(dec->vdec, DECODE_VALUE, disp);
        string_buffer_print(output, ",3");
        break;
    }
}

/* Decodes a A-group class instruction.
 * Output is appended to `output`.
 */
static
void decode_agroup(struct decoder *dec,
                   const struct nova_insn *ni)
{
    struct string_buffer *output;
    uint16_t src_ac;
    uint16_t dest_ac;
    uint16_t afunc;
    uint16_t shift;
    uint16_t carry;
    int no_load;
    uint16_t skip;

    output = dec->output;
    skip = ni->insn & 7;
    no_load = (ni->insn >> 3) & 1;
    carry = (ni->insn >> 4) & 3;
    shift = (ni->insn >> 6) & 3;
    afunc = (ni->insn >> 8) & 7;
    dest_ac = (ni->insn >> 11) & 3;
    src_ac = (ni->insn >> 13) & 3;

    switch (afunc) {
    case 0:
        string_buffer_print(output, "COM");
        break;
    case 1:
        string_buffer_print(output, "NEG");
        break;
    case 2:
        string_buffer_print(output, "MOV");
        break;
    case 3:
        string_buffer_print(output, "INC");
        break;
    case 4:
        string_buffer_print(output, "ADC");
        break;
    case 5:
        string_buffer_print(output, "SUB");
        break;
    case 6:
        string_buffer_print(output, "ADD");
        break;
    case 7:
        string_buffer_print(output, "AND");
        break;
    }

    switch (carry) {
    case 0:
        break;
    case 1:
        string_buffer_print(output, "Z");
        break;
    case 2:
        string_buffer_print(output, "O");
        break;
    case 3:
        string_buffer_print(output, "C");
        break;
    }

    switch (shift) {
    case 0:
        break;
    case 1:
        string_buffer_print(output, "L");
        break;
    case 2:
        string_buffer_print(output, "R");
        break;
    case 3:
        string_buffer_print(output, "S");
        break;
    }

    if (no_load) {
        string_buffer_print(output, "#");
    }

    string_buffer_print(output, " %o %o", dest_ac, src_ac);

    switch (skip) {
    case 0:
        break;
    case 1:
        string_buffer_print(output, " SKP");
        break;
    case 2:
        string_buffer_print(output, " SZC");
        break;
    case 3:
        string_buffer_print(output, " SNC");
        break;
    case 4:
        string_buffer_print(output, " SZR");
        break;
    case 5:
        string_buffer_print(output, " SNR");
        break;
    case 6:
        string_buffer_print(output, " SEZ");
        break;
    case 7:
        string_buffer_print(output, " SBN");
        break;
    }
}

/* Decodes a S-group class instruction. */
static
void decode_sgroup(struct decoder *dec,
                   const struct nova_insn *ni)
{
    struct string_buffer *output;
    uint16_t aug_func;
    uint16_t disp;

    output = dec->output;
    disp = (ni->insn & 0xFF);
    /* Sign extends. */
    if (disp & 0x80) disp |= 0xFF00;

    aug_func = (ni->insn >> 8) & 31;

    switch (aug_func) {
    case 0:
        string_buffer_print(output, "CYCLE ");
        decode_value(dec->vdec, DECODE_VALUE, disp & 0x0F);
        break;
    case 1:
        string_buffer_print(output, "<unknown> (U5)");
        break;
    case 2:
        switch (disp) {
        case 0: string_buffer_print(output, "DIR"); break;
        case 1: string_buffer_print(output, "EIR"); break;
        case 2: string_buffer_print(output, "BRI"); break;
        case 3: string_buffer_print(output, "RCLK"); break;
        case 4: string_buffer_print(output, "SIO"); break;
        case 5: string_buffer_print(output, "BLT"); break;
        case 6: string_buffer_print(output, "BLKS"); break;
        case 7: string_buffer_print(output, "SIT"); break;
        case 8: string_buffer_print(output, "JMPRAM"); break;
        case 9: string_buffer_print(output, "RDRAM"); break;
        case 10: string_buffer_print(output, "WRTRAM"); break;
        case 11: string_buffer_print(output, "DIRS"); break;
        case 12: string_buffer_print(output, "VERS"); break;
        case 13: string_buffer_print(output, "DREAD"); break;
        case 14: string_buffer_print(output, "DWRITE"); break;
        case 15: string_buffer_print(output, "DEXCH"); break;
        case 16: string_buffer_print(output, "MUL"); break;
        case 17: string_buffer_print(output, "DIV"); break;
        case 18: string_buffer_print(output, "DIAGNOSE1"); break;
        case 19: string_buffer_print(output, "DIAGNOSE2"); break;
        case 20: string_buffer_print(output, "BITBLT"); break;
        case 21: string_buffer_print(output, "XMLDA"); break;
        case 22: string_buffer_print(output, "XMSTA"); break;
        default:
            string_buffer_print(output, "<unknown>");
            break;
        }
        break;
    case 3:
        string_buffer_print(output, "<unknown> (U6)");
        break;
    case 4:
        string_buffer_print(output, "<unknown> (U7)");
        break;
    case 9:
        string_buffer_print(output, "JSRII ");
        decode_value(dec->vdec, DECODE_VALUE, disp & 0xFF);
        break;
    case 10:
        string_buffer_print(output, "JSRIS ");
        decode_value(dec->vdec, DECODE_VALUE, disp & 0xFF);
        break;
    case 14:
        string_buffer_print(output, "CONVERT ");
        decode_value(dec->vdec, DECODE_VALUE, disp & 0xFF);
        break;
    case 31:
        string_buffer_print(output, "<unknown> (TRAP)");
        break;
    default:
        string_buffer_print(output, "<unknown> (RAMTRAP)");
        break;
    }
}


void nova_insn_decode(struct decoder *dec,
                      const struct nova_insn *ni)
{
    if (dec->error) return;

    switch ((ni->insn >> 13) & 7) {
    case 0:
        decode_jgroup(dec, ni);
        break;
    case 1:
    case 2:
        decode_mgroup(dec, ni);
        break;
    case 3:
        decode_sgroup(dec, ni);
        break;
    default:
        decode_agroup(dec, ni);
        break;
    }
}
