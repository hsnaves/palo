#include <stddef.h>
#include <stdint.h>

#include "microcode/microcode.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Functions. */

void microcode_predecode(struct microcode *mc,
                         enum system_type sys_type,
                         uint16_t address, uint32_t mcode,
                         uint8_t task)
{
    mc->sys_type = sys_type;
    mc->address = address;
    mc->mcode = mcode;
    mc->task = task;

    mc->rsel = MICROCODE_RSEL(mc->mcode);
    mc->aluf = MICROCODE_ALUF(mc->mcode);
    mc->bs = MICROCODE_BS(mc->mcode);
    mc->f1 = MICROCODE_F1(mc->mcode);
    mc->f2 = MICROCODE_F2(mc->mcode);
    mc->load_t = MICROCODE_T(mc->mcode);
    mc->load_l = MICROCODE_L(mc->mcode);
    mc->next = MICROCODE_NEXT(mc->mcode);

    mc->load_t_from_alu = LOAD_T_FROM_ALU(mc->aluf);
    mc->use_constant = (mc->f1 == F1_CONSTANT
                        || mc->f2 == F2_CONSTANT);
    mc->bs_use_crom = BS_USE_CROM(mc->bs);
    mc->const_addr = CONST_ADDR(mc->rsel, mc->bs);
    mc->ram_task = ((1 << mc->task) & TASK_RAM_MASK);
}

/* Decodes the non-data function part of the instruction. */
static
void decode_nondata_function(struct decoder *dec)
{
    const char *f1_op;
    const char *f2_op;

    f1_op = "";
    switch (dec->mc.f1) {
    case F1_NONE:
    case F1_LOAD_MAR:
    case F1_LLSH1:
    case F1_LRSH1:
    case F1_LLCY8:
    case F1_CONSTANT:
        break;
    case F1_TASK:
        f1_op = "TASK";
        break;
    case F1_BLOCK:
        f1_op = "BLOCK";
        break;
    default:
        if (dec->mc.ram_task) {
            switch (dec->mc.f1) {
            case F1_RAM_SWMODE:
                if (dec->mc.task == TASK_EMULATOR) {
                    f1_op = "SWMODE";
                } else {
                    /* Invalid F1 function. */
                    dec->error = TRUE;
                    return;
                }
                goto process_f2;
            case F1_RAM_WRTRAM:
                f1_op = "WRTRAM";
                goto process_f2;
            case F1_RAM_RDRAM:
                f1_op = "RDRAM";
                goto process_f2;
            case F1_RAM_LOAD_SRB:
                if (dec->mc.task != TASK_EMULATOR)
                    goto process_f2;
                break;
            }
        }

        switch (dec->mc.task) {
        case TASK_EMULATOR:
            switch (dec->mc.f1) {
            case F1_EMU_STARTF:
                f1_op = "STARTF";
                break;
            case F1_EMU_LOAD_RMR:
            case F1_EMU_LOAD_ESRB:
            case F1_EMU_RSNF:
                /* Nothing to do here. */
                break;
            default:
                /* Invalid F1 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_DISK_SECTOR:
        case TASK_DISK_WORD:
            switch (dec->mc.f1) {
            case F1_DSK_STROBE:
                f1_op = "STROBE";
                break;
            case F1_DSK_INCRECNO:
                f1_op = "INCRECNO";
                break;
            case F1_DSK_CLRSTAT:
                f1_op = "CLRSTAT";
                break;
            case F1_DSK_LOAD_KSTAT:
            case F1_DSK_LOAD_KCOMM:
            case F1_DSK_LOAD_KADR:
            case F1_DSK_LOAD_KDATA:
                /* Nothing to do here. */
                break;
            default:
                /* Invalid F1 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_ETHERNET:
            switch (dec->mc.f1) {
            case F1_ETH_EILFCT:
                f1_op = "EILFCT";
                break;
            case F1_ETH_EPFCT:
                f1_op = "EPFCT";
                break;
            case F1_ETH_EWFCT:
                f1_op = "EWFCT";
                break;
            default:
                /* Invalid F1 function. */
                dec->error = TRUE;
                return;
            }
            break;
        default:
            /* Invalid F1 function. */
            dec->error = TRUE;
            return;
        }
        break;
    }

process_f2:
    f2_op = "";
    switch (dec->mc.f2) {
    case F2_NONE:
    case F2_STORE_MD:
    case F2_CONSTANT:
        break;
    case F2_BUSEQ0:
        f2_op = "BUS=0";
        break;
    case F2_SHLT0:
        f2_op = "SH<0";
        break;
    case F2_SHEQ0:
        f2_op = "SH=0";
        break;
    case F2_BUS:
        f2_op = "BUS";
        break;
    case F2_ALUCY:
        f2_op = "ALUCY";
        break;
    default:
        switch (dec->mc.task) {
        case TASK_EMULATOR:
            switch (dec->mc.f2) {
            case F2_EMU_BUSODD:
                f2_op = "BUSODD";
                break;
            case F2_EMU_IDISP:
                f2_op = "IDISP";
                break;
            case F2_EMU_MAGIC:
            case F2_EMU_LOAD_DNS:
            case F2_EMU_ACDEST:
            case F2_EMU_LOAD_IR:
            case F2_EMU_ACSOURCE:
                /* Nothing to do here. */
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_DISK_SECTOR:
        case TASK_DISK_WORD:
            switch (dec->mc.f2) {
            case F2_DSK_INIT:
                f2_op = "INIT";
                break;
            case F2_DSK_RWC:
                f2_op = "RWC";
                break;
            case F2_DSK_RECNO:
                f2_op = "RECNO";
                break;
            case F2_DSK_XFRDAT:
                f2_op = "XFRDAT";
                break;
            case F2_DSK_SWRNRDY:
                f2_op = "SWRNRDY";
                break;
            case F2_DSK_NFER:
                f2_op = "NFER";
                break;
            case F2_DSK_STROBON:
                f2_op = "STROBON";
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_ETHERNET:
            switch (dec->mc.f2) {
            case F2_ETH_EOSFCT:
                f2_op = "EOSFCT";
                break;
            case F2_ETH_ERBFCT:
                f2_op = "ERBFCT";
                break;
            case F2_ETH_EEFCT:
                f2_op = "EEFCT";
                break;
            case F2_ETH_EBFCT:
                f2_op = "EBFCT";
                break;
            case F2_ETH_ECBFCT:
                f2_op = "ECBFCT";
                break;
            case F2_ETH_EISFCT:
                f2_op = "EISFCT";
                break;
            case F2_ETH_EODFCT:
                /* Nothing to do here. */
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_DISPLAY_WORD:
            switch (dec->mc.f2) {
            case F2_DW_LOAD_DDR:
                /* Nothing to do here. */
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_CURSOR:
            switch (dec->mc.f2) {
            case F2_CUR_LOAD_XPREG:
            case F2_CUR_LOAD_CSR:
                /* Nothing to do here. */
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_DISPLAY_HORIZONTAL:
            switch (dec->mc.f2) {
            case F2_DH_EVENFIELD:
                f2_op = "EVENFIELD";
                break;
            case F2_DH_SETMODE:
                f2_op = "SETMODE";
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        case TASK_DISPLAY_VERTICAL:
            switch (dec->mc.f2) {
            case F2_DV_EVENFIELD:
                f2_op = "EVENFIELD";
                break;
            default:
                /* Invalid F2 function. */
                dec->error = TRUE;
                return;
            }
            break;
        default:
            /* Invalid F2 function. */
            dec->error = TRUE;
            return;
        }
        break;
    }

    string_buffer_print(dec->output, "%s%s%s%s",
                        f1_op,
                        (f1_op[0] && f2_op[0]) ? ", " : "",
                        f2_op,
                        (f1_op[0] || f2_op[0]) ? ", " : "");
}

/* Decodes the bus RHS (source). */
static
void decode_bus_rhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if (dec->mc.use_constant) {
        /* Decode the constant. */
        (*dec->dec_cb)(dec, DECODE_CONST, dec->mc.const_addr);
        return;
    }

    switch (dec->mc.bs) {
    case BS_READ_R:
        if (dec->mc.task == TASK_EMULATOR && dec->mc.rsel == 0) {
            if (dec->mc.f2 == F2_EMU_ACDEST) {
                string_buffer_print(output, "ACDEST");
                break;
            } else if (dec->mc.f2 == F2_EMU_ACSOURCE) {
                string_buffer_print(output, "ACSOURCE");
                break;
            }
        }
        (*dec->dec_cb)(dec, DECODE_REG, dec->mc.rsel);
        break;
    case BS_LOAD_R:
        string_buffer_print(output, "0");
        break;
    case BS_NONE:
        if (dec->mc.task == TASK_EMULATOR && dec->mc.f1 == F1_EMU_RSNF) {
            string_buffer_print(output, "RSNF");
        } else if (dec->mc.task == TASK_ETHERNET) {
            if (dec->mc.f1 == F1_ETH_EILFCT) {
                string_buffer_print(output, "EILFCT");
            } else if (dec->mc.f1 == F1_ETH_EPFCT) {
                string_buffer_print(output, "EPFCT");
            } else {
                string_buffer_print(output, "-1");
            }
        } else {
            string_buffer_print(output, "-1");
        }
        break;
    case BS_READ_MD:
        string_buffer_print(output, "MD");
        break;
    case BS_READ_MOUSE:
        string_buffer_print(output, "MOUSE");
        break;
    case BS_READ_DISP:
        string_buffer_print(output, "DISP");
        break;
    default:
        if (dec->mc.ram_task) {
            if (dec->mc.bs == BS_RAM_READ_S_LOCATION) {
                if (dec->mc.rsel == 0) {
                    string_buffer_print(output, "M");
                } else {
                    (*dec->dec_cb)(dec, DECODE_REG,
                                   dec->mc.rsel | (R_MASK + 1));
                }
                break;
            } else if (dec->mc.bs == BS_RAM_LOAD_S_LOCATION) {
                string_buffer_print(output, "0");
                break;
            }
        } else if (dec->mc.task == TASK_ETHERNET) {
            if (dec->mc.bs == BS_ETH_EIDFCT) {
                string_buffer_print(output, "EIDFCT");
                break;
            }
        } else if ((dec->mc.task == TASK_DISK_SECTOR)
                   || (dec->mc.task == TASK_DISK_WORD)) {
            if (dec->mc.bs == BS_DSK_READ_KSTAT) {
                string_buffer_print(output, "KSTAT");
                break;
            } else if (dec->mc.bs == BS_DSK_READ_KDATA) {
                string_buffer_print(output, "KDATA");
                break;
            }
        }
        string_buffer_print(output, "<invalid>");
        break;
    }
}

/* Decodes the bus LHS (destinations).
 * If `force` parameter is set to TRUE, the output is produced
 * even when the ALUF is ALU_BUS.
 */
static
void decode_bus_lhs(struct decoder *dec, int force)
{
    struct string_buffer *output;
    output = dec->output;

    /* If ALUF is BUS, then we skip the BUS assignments, and
     * merge them with the ALU assigments.
     */
    if (dec->mc.aluf == ALU_BUS && !force) return;

    if (dec->mc.load_t && (!dec->mc.load_t_from_alu)) {
        string_buffer_print(output, "T<- ");
    }

    if (dec->mc.task != TASK_EMULATOR) {
        if (dec->mc.f1 == F1_RAM_LOAD_SRB && dec->mc.ram_task) {
            string_buffer_print(output, "SRB<- ");
        }
    }

    switch (dec->mc.task) {
    case TASK_EMULATOR:
        switch (dec->mc.f1) {
        case F1_EMU_LOAD_RMR:
            string_buffer_print(output, "RMR<- ");
            break;
        case F1_EMU_LOAD_ESRB:
            string_buffer_print(output, "ESRB<- ");
            break;
        }
        break;
    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (dec->mc.f1) {
        case F1_DSK_LOAD_KSTAT:
            string_buffer_print(output, "KSTAT<- ");
            break;
        case F1_DSK_LOAD_KCOMM:
            string_buffer_print(output, "KCOMM<- ");
            break;
        case F1_DSK_LOAD_KADR:
            string_buffer_print(output, "KADR<- ");
            break;
        case F1_DSK_LOAD_KDATA:
            string_buffer_print(output, "KDATA<- ");
            break;
        }
        break;
    }

    switch (dec->mc.f2) {
    case F2_STORE_MD:
        if (dec->mc.f1 != F1_LOAD_MAR || dec->mc.sys_type == ALTO_I) {
            /* TODO: On Alto I MAR<- and <-MD in the same
             * microinstruction should be illegal.
             */
            string_buffer_print(output, "MD<- ");
        }
        break;
    default:
        switch (dec->mc.task) {
        case TASK_EMULATOR:
            switch (dec->mc.f2) {
            case F2_EMU_LOAD_DNS:
                string_buffer_print(output, "DNS<- ");
                break;
            case F2_EMU_LOAD_IR:
                string_buffer_print(output, "IR<- ");
                break;
            }
            break;
        case TASK_ETHERNET:
            switch (dec->mc.f2) {
            case F2_ETH_EODFCT:
                string_buffer_print(output, "EODFCT<- ");
                break;
            }
            break;
        case TASK_DISPLAY_WORD:
            switch (dec->mc.f2) {
            case F2_DW_LOAD_DDR:
                string_buffer_print(output, "DDR<- ");
                break;
            }
            break;
        case TASK_CURSOR:
            switch (dec->mc.f2) {
            case F2_CUR_LOAD_XPREG:
                string_buffer_print(output, "XPREG<- ");
                break;
            case F2_CUR_LOAD_CSR:
                string_buffer_print(output, "CSR<- ");
                break;
            }
            break;
        }
    }
}

/* Decodes the bus assignments. */
static
void decode_bus_assign(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    decode_bus_lhs(dec, FALSE);
    dec->has_bus_assignment = (len != string_buffer_length(output));
    if (!dec->has_bus_assignment) return;
    decode_bus_rhs(dec);
    string_buffer_print(output, ", ");
}

/* Decodes the alu RHS (source). */
static
void decode_alu_rhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if (dec->mc.aluf != ALU_T) {
        decode_bus_rhs(dec);
    }

    switch (dec->mc.aluf) {
    case ALU_BUS:
        break;
    case ALU_T:
        string_buffer_print(output, "T");
        break;
    case ALU_BUS_OR_T:
        string_buffer_print(output, " OR T");
        break;
    case ALU_BUS_AND_T:
        string_buffer_print(output, " AND T");
        break;
    case ALU_BUS_XOR_T:
        string_buffer_print(output, " XOR T");
        break;
    case ALU_BUS_PLUS_1:
        string_buffer_print(output, " + 1");
        break;
    case ALU_BUS_MINUS_1:
        string_buffer_print(output, " - 1");
        break;
    case ALU_BUS_PLUS_T:
        string_buffer_print(output, " + T");
        break;
    case ALU_BUS_MINUS_T:
        string_buffer_print(output, " - T");
        break;
    case ALU_BUS_MINUS_T_MINUS_1:
        string_buffer_print(output, " - T - 1");
        break;
    case ALU_BUS_PLUS_T_PLUS_1:
        string_buffer_print(output, " + T + 1");
        break;
    case ALU_BUS_PLUS_SKIP:
        string_buffer_print(output, " + SKIP");
        break;
    case ALU_BUS_AND_T_WB:
        string_buffer_print(output, " . T");
        break;
    case ALU_BUS_AND_NOT_T:
        string_buffer_print(output, " AND NOT T");
        break;
    }
}

/* Decodes the alu LHS (destinations). */
static
void decode_alu_lhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if (dec->mc.aluf == ALU_BUS) {
        decode_bus_lhs(dec, TRUE);
    }

    if (dec->mc.load_t && dec->mc.load_t_from_alu) {
        string_buffer_print(output, "T<- ");
    }

    if (dec->mc.load_l) {
        if (dec->mc.task == TASK_EMULATOR) {
            string_buffer_print(output, "M<- ");
        }
        string_buffer_print(output, "L<- ");
    }

    if (dec->mc.f1 == F1_LOAD_MAR) {
        /* TODO: On Alto I MAR<- and <-MD in the same
         * microinstruction should be illegal.
         */
        if (dec->mc.f2 == F2_STORE_MD && dec->mc.sys_type != ALTO_I) {
            string_buffer_print(output, "XMAR<- ");
        } else {
            string_buffer_print(output, "MAR<- ");
        }
    }
}

/* Decodes the alu assignments. */
static
void decode_alu_assign(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    decode_alu_lhs(dec);
    dec->has_alu_assignment = (len != string_buffer_length(output));
    if (!dec->has_alu_assignment) return;
    decode_alu_rhs(dec);
    string_buffer_print(output, ", ");
}

/* Decodes the L register RHS (source). */
static
void decode_lreg_rhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    switch (dec->mc.f1) {
    case F1_LLSH1:
        if (dec->mc.f2 == F2_EMU_MAGIC) {
            string_buffer_print(output, "L MLSH 1");
        } else {
            string_buffer_print(output, "L LSH 1");
        }
        break;
    case F1_LRSH1:
        if (dec->mc.f2 == F2_EMU_MAGIC) {
            string_buffer_print(output, "L MRSH 1");
        } else {
            string_buffer_print(output, "L RSH 1");
        }
        break;
    case F1_LLCY8:
        string_buffer_print(output, "L LCY 8");
        break;
    default:
        string_buffer_print(output, "L");
        break;
    }
}

/* Decodes the L register LHS (destinations). */
static
void decode_lreg_lhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if ((!dec->mc.use_constant) && dec->mc.bs == BS_LOAD_R) {
        (*dec->dec_cb)(dec, DECODE_REG, dec->mc.rsel);
        string_buffer_print(output, "<- ");
    }
}

/* Decodes assigments from the L register. */
static
void decode_lreg_assign(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    decode_lreg_lhs(dec);
    if (len == string_buffer_length(output)) return;
    decode_lreg_rhs(dec);
    string_buffer_print(output, ", ");
}

/* Decodes the M register RHS (source). */
static
void decode_mreg_rhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    string_buffer_print(output, "M");
}

/* Decodes the M register LHS (destinations). */
static
void decode_mreg_lhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if ((!dec->mc.use_constant) && dec->mc.ram_task
        && dec->mc.bs == BS_RAM_LOAD_S_LOCATION) {

        (*dec->dec_cb)(dec, DECODE_REG,
                       dec->mc.rsel | (R_MASK + 1));
        string_buffer_print(output, "<- ");
    }
}

/* Decodes assigments from the M register. */
static
void decode_mreg_assign(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    decode_mreg_lhs(dec);
    if (len == string_buffer_length(output)) return;
    decode_mreg_rhs(dec);
    string_buffer_print(output, ", ");
}

/* Decodes the SINK (bus) register destinations. */
static
void decode_sink_bus_lhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if (dec->has_bus_assignment) return;
    if (dec->mc.aluf != ALU_T) {
        if (dec->has_alu_assignment) return;
    }

    if (dec->mc.use_constant)
        goto do_sink;

    switch (dec->mc.bs) {
    case BS_READ_R:
        if (dec->mc.rsel != 0) break;
        if (dec->mc.task != TASK_EMULATOR)
            return;
        if (dec->mc.f2 != F2_EMU_ACDEST && dec->mc.f2 != F2_EMU_ACSOURCE)
            return;
        break;
    case BS_LOAD_R:
        return;
    case BS_NONE:
        if (dec->mc.task == TASK_EMULATOR
            && dec->mc.f1 == F1_EMU_RSNF)
            break;
        else if (dec->mc.task == TASK_ETHERNET
                 && dec->mc.f1 == F1_ETH_EILFCT)
            break;
        else if (dec->mc.task == TASK_ETHERNET
                 && dec->mc.f1 == F1_ETH_EPFCT)
            break;
        return;
    case BS_READ_MD:
    case BS_READ_MOUSE:
    case BS_READ_DISP:
        break;
    default:
        if (dec->mc.task == TASK_ETHERNET
            && dec->mc.bs == BS_ETH_EIDFCT)
            break;

        if ((dec->mc.task == TASK_DISK_SECTOR)
            || (dec->mc.task == TASK_DISK_WORD)) {
            if (dec->mc.bs == BS_DSK_READ_KSTAT) {
                break;
            } else if (dec->mc.bs == BS_DSK_READ_KDATA) {
                break;
            }
        }
        return;
    }

do_sink:
    string_buffer_print(output, "SINK<- ");
}

/* Decodes assigments to the SINK register (for bus). */
static
void decode_sink_bus_assign(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    decode_sink_bus_lhs(dec);
    if (len == string_buffer_length(output)) return;
    decode_bus_rhs(dec);
    string_buffer_print(output, ", ");
}

/* Decodes the SINK RHS (source) (for constants). */
static
void decode_sink_const_rhs(struct decoder *dec)
{
    (dec->dec_cb)(dec, DECODE_REG, dec->mc.rsel);
}

/* Decodes the SINK RHS (destinations) (for constants). */
static
void decode_sink_const_lhs(struct decoder *dec)
{
    struct string_buffer *output;

    output = dec->output;
    if (dec->mc.use_constant) return;
    if (!dec->mc.bs_use_crom) return;
    if (dec->mc.rsel == 0) return;

    string_buffer_print(output, "SINK<- ");
}

/* Decodes assigments to the SINK register (for constants). */
static
void decode_sink_const_assign(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    decode_sink_const_lhs(dec);
    if (len == string_buffer_length(output)) return;
    decode_sink_const_rhs(dec);
    string_buffer_print(output, ", ");
}

/* Decodes the GOTO part of the instruction.
 * The original length of the `output` buffer (at the begining of
 * decoder_decode()) is in `orig_len`.
 */
static
void decode_goto(struct decoder *dec, size_t orig_len)
{
    struct string_buffer *output;
    size_t len;

    output = dec->output;
    len = string_buffer_length(output);
    string_buffer_print(output, ":");
    (*dec->dec_cb)(dec, DECODE_LABEL, dec->mc.next);
    if (len + 1 == string_buffer_length(output)) {
        /* Rewinds the ":" at the end of the string. */
        string_buffer_rewind(output, 1);
        if (len != orig_len) {
            /* Rewinds the ", " at the end of the string. */
            string_buffer_rewind(output, 2);
        }
    }
}

void decoder_decode(struct decoder *dec)
{
    struct string_buffer *output;
    size_t len;

    dec->error = FALSE;
    dec->has_bus_assignment = FALSE;
    dec->has_alu_assignment = FALSE;

    output = dec->output;
    len = string_buffer_length(output);

    decode_nondata_function(dec);
    if (dec->error) goto decode_error;

    decode_bus_assign(dec);
    if (dec->error) goto decode_error;

    decode_alu_assign(dec);
    if (dec->error) goto decode_error;

    decode_lreg_assign(dec);
    if (dec->error) goto decode_error;

    decode_mreg_assign(dec);
    if (dec->error) goto decode_error;

    decode_sink_bus_assign(dec);
    if (dec->error) goto decode_error;

    decode_sink_const_assign(dec);
    if (dec->error) goto decode_error;

    decode_goto(dec, len);
    if (dec->error) goto decode_error;
    return;

decode_error:
    if (string_buffer_length(output) > len) {
        len = string_buffer_length(output) - len;
        string_buffer_rewind(output, len);
    }
    string_buffer_print(output, "<invalid>");
}
