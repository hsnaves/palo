#include <stddef.h>
#include <stdint.h>

#include "microcode/microcode.h"
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

uint16_t microcode_guess_tasks(const struct microcode *mc)
{
    uint16_t task_mask;
    uint16_t aux_mask;

    task_mask = TASK_VALID_MASK;

    switch (mc->f1) {
    case 010: /* F1_RAM_SWMODE */
        task_mask &= (1 << TASK_EMULATOR);
        break;
    case 011: /* F1_EMU_WRTRAM, F1_DSK_STROBE */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD));
        break;
    case 012: /* F1_EMU_RDRAM,  F1_DSK_LOAD_KSTAT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD));
        break;
    case 013: /* F1_EMU_LOAD_RMR, F1_DSK_INCRECNO, F1_ETH_EILFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        break;
    case 014: /* F1_DSK_CLRSTAT, F1_ETH_EPFCT */
        task_mask &= ((1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        break;
    case 015: /* F1_EMU_LOAD_ESRB, F1_DSK_LOAD_KCOMM, F1_ETH_EWFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        break;
    case 016: /* F1_EMU_RSNF, F1_DSK_LOAD_KADR */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD));
        break;
    case 017: /* F1_EMU_STARTF, F1_DSK_LOAD_KDATA */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD));
    }

    switch (mc->f2) {
    case 010: /* F2_EMU_BUSODD, F2_DSK_INIT, F2_ETH_EODFCT,
               * F2_DW_LOAD_DDR, F2_CUR_LOAD_XPREG, F2_DH_EVENFIELD
               * F2_DV_EVENFIELD
               */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET)
                      | (1 << TASK_DISPLAY_WORD)
                      | (1 << TASK_CURSOR)
                      | (1 << TASK_DISPLAY_HORIZONTAL)
                      | (1 << TASK_DISPLAY_VERTICAL));
        break;
    case 011: /* F2_EMU_MAGIC, F2_DSK_RWC, F2_ETH_EOSFCT,
               * F2_CUR_LOAD_CSR, F2_DH_SETMODE
               */
        aux_mask = ((1 << TASK_DISK_SECTOR)
                    | (1 << TASK_DISK_WORD)
                    | (1 << TASK_ETHERNET)
                    | (1 << TASK_CURSOR)
                    | (1 << TASK_DISPLAY_HORIZONTAL));
        if (mc->f1 == F1_LLSH1 || mc->f1 == F1_LRSH1) {
            aux_mask |= 1 << TASK_EMULATOR;
        }
        task_mask &= aux_mask;
        break;
    case 012: /* F2_EMU_LOAD_DNS, F2_DSK_RECNO, F2_ETH_ERBFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        break;
    case 013: /* F2_EMU_ACDEST, F2_DSK_XFRDAT, F2_ETH_EEFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        aux_mask = ((1 << TASK_DISK_SECTOR)
                    | (1 << TASK_DISK_WORD)
                    | (1 << TASK_ETHERNET));
        if (mc->bs == BS_READ_R && mc->rsel == 0) {
            aux_mask |= 1 << TASK_EMULATOR;
        }
        task_mask &= aux_mask;
        break;
    case 014: /* F2_EMU_LOAD_IR, F2_DSK_SWRNRDY, F2_ETH_EBFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        break;
    case 015: /* F2_EMU_IDISP, F2_DSK_NFER, F2_ETH_ECBFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        break;
    case 016: /* F2_EMU_ACSOURCE, F2_DSK_STROBON, F2_ETH_EISFCT */
        task_mask &= ((1 << TASK_EMULATOR)
                      | (1 << TASK_DISK_SECTOR)
                      | (1 << TASK_DISK_WORD)
                      | (1 << TASK_ETHERNET));
        aux_mask = ((1 << TASK_DISK_SECTOR)
                    | (1 << TASK_DISK_WORD)
                    | (1 << TASK_ETHERNET));
        if (mc->bs == BS_READ_R && mc->rsel == 0) {
            aux_mask |= 1 << TASK_EMULATOR;
        }
        task_mask &= aux_mask;
        break;
    case 017:
        task_mask &= 0;
        break;
    }

    if (!mc->use_constant) {
        switch (mc->bs) {
        case BS_TASK_SPECIFIC1:
            /* BS_EMU_READ_S_LOCATION, BS_DSK_READ_KSTAT */
            task_mask &= ((1 << TASK_EMULATOR)
                          | (1 << TASK_DISK_SECTOR)
                          | (1 << TASK_DISK_WORD));
            break;
        case BS_TASK_SPECIFIC2:
            /* BS_EMU_LOAD_S_LOCATION, BS_DSK_READ_KDATA, BS_ETH_EIDFCT */
            task_mask &= ((1 << TASK_EMULATOR)
                          | (1 << TASK_DISK_SECTOR)
                          | (1 << TASK_DISK_WORD)
                          | (1 << TASK_ETHERNET));
            break;
        }
    }

    return task_mask;
}

uint32_t microcode_next_mask(const struct microcode *mc)
{
    uint32_t output;

    output = 0;
    switch (mc->f2) {
    case F2_BUSEQ0: output |= 0x1; break;
    case F2_SHLT0:  output |= 0x1; break;
    case F2_SHEQ0:  output |= 0x1; break;
    case F2_BUS:
        /* Need to compute a more acurate mask. */
        if (mc->bs_use_crom || mc->f1 == F1_CONSTANT) {
            output |= 0xFFFF | NEXT_MASK_BUS | NEXT_MASK_CONSTANT;
        } else if (mc->bs == BS_LOAD_R) {
            output |= NEXT_MASK_BUS;
        } else {
            output |= 0xFFFF | NEXT_MASK_BUS;
        }
        break;
    case F2_ALUCY:  output |= 0x1; break;
    }

    switch (mc->task) {
    case TASK_EMULATOR:
        switch (mc->f2) {
        case F2_EMU_BUSODD:   output |= 0x1; break;
        case F2_EMU_LOAD_IR:
            if (mc->bs_use_crom || mc->f1 == F1_CONSTANT) {
                output |= NEXT_MASK_CONSTANT;
            }
            if (mc->bs != BS_LOAD_R) {
                output |= 0xF;
            }
            break;
        case F2_EMU_IDISP:    output |= 0xF; break;
        case F2_EMU_ACSOURCE: output |= 0xF; break;
        }
        break;
    case TASK_ETHERNET:
        switch (mc->f2) {
        case F2_ETH_ERBFCT:   output |= 0xC; break;
        case F2_ETH_EBFCT:    output |= 0xC; break;
        case F2_ETH_ECBFCT:   output |= 0x4; break;
        }
        break;
    case TASK_DISPLAY_HORIZONTAL:
        switch (mc->f2) {
        case F2_DH_EVENFIELD: output |= 0x1; break;
        case F2_DH_SETMODE:   output |= 0x1; break;
        }
        break;
    case TASK_DISPLAY_VERTICAL:
        switch (mc->f2) {
        case F2_DV_EVENFIELD: output |= 0x1; break;
        }
        break;
    case TASK_DISK_WORD:
    case TASK_DISK_SECTOR:
        switch (mc->f2) {
        case F2_DSK_INIT:
            output |= (0x1F | NEXT_MASK_DSK_INIT);
            break;
        case F2_DSK_RWC:      output |= 0x3; break;
        case F2_DSK_RECNO:    output |= 0x3; break;
        case F2_DSK_XFRDAT:   output |= 0x1; break;
        case F2_DSK_SWRNRDY:  output |= 0x1; break;
        case F2_DSK_NFER:     output |= 0x1; break;
        case F2_DSK_STROBON:  output |= 0x1; break;
        }
        break;
    }

    return output;
}

/* Decodes the non-data function part of the instruction.
 * The output string is placed in `output`.
 */
static
void decode_nondata_function(struct decoder *dec,
                             struct string_buffer *output)
{
    char f1_buffer[20];
    char f2_buffer[20];
    struct string_buffer f1_op;
    struct string_buffer f2_op;

    f1_op.buf = f1_buffer;
    f1_op.buf_size = sizeof(f1_buffer);
    string_buffer_reset(&f1_op);

    f2_op.buf = f2_buffer;
    f2_op.buf_size = sizeof(f2_buffer);
    string_buffer_reset(&f2_op);

    f1_buffer[0] = '\0';
    switch (dec->mc.f1) {
    case F1_NONE:
    case F1_LOAD_MAR:
    case F1_LLSH1:
    case F1_LRSH1:
    case F1_LLCY8:
    case F1_CONSTANT:
        break;
    case F1_TASK:
        string_buffer_print(&f1_op, "TASK");
        break;
    case F1_BLOCK:
        string_buffer_print(&f1_op, "BLOCK");
        break;
    default:
        if (dec->mc.ram_task) {
            switch (dec->mc.f1) {
            case F1_RAM_SWMODE:
                if (dec->mc.task == TASK_EMULATOR) {
                    string_buffer_print(&f1_op, "SWMODE");
                } else {
                    /* Invalid F1 function. */
                    dec->error = TRUE;
                    return;
                }
                goto process_f2;
            case F1_RAM_WRTRAM:
                string_buffer_print(&f1_op, "WRTRAM");
                goto process_f2;
            case F1_RAM_RDRAM:
                string_buffer_print(&f1_op, "RDRAM");
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
                string_buffer_print(&f1_op, "STARTF");
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
                string_buffer_print(&f1_op, "STROBE");
                break;
            case F1_DSK_INCRECNO:
                string_buffer_print(&f1_op, "INCRECNO");
                break;
            case F1_DSK_CLRSTAT:
                string_buffer_print(&f1_op, "CLRSTAT");
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
                string_buffer_print(&f1_op, "EILFCT");
                break;
            case F1_ETH_EPFCT:
                string_buffer_print(&f1_op, "EPFCT");
                break;
            case F1_ETH_EWFCT:
                string_buffer_print(&f1_op, "EWFCT");
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
    f2_buffer[0] = '\0';
    switch (dec->mc.f2) {
    case F2_NONE:
    case F2_STORE_MD:
    case F2_CONSTANT:
        break;
    case F2_BUSEQ0:
        string_buffer_print(&f2_op, "BUS=0");
        break;
    case F2_SHLT0:
        string_buffer_print(&f2_op, "SH<0");
        break;
    case F2_SHEQ0:
        string_buffer_print(&f2_op, "SH=0");
        break;
    case F2_BUS:
        string_buffer_print(&f2_op, "BUS");
        break;
    case F2_ALUCY:
        string_buffer_print(&f2_op, "ALUCY");
        break;
    default:
        switch (dec->mc.task) {
        case TASK_EMULATOR:
            switch (dec->mc.f2) {
            case F2_EMU_BUSODD:
                string_buffer_print(&f2_op, "BUSODD");
                break;
            case F2_EMU_IDISP:
                string_buffer_print(&f2_op, "IDISP");
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
                string_buffer_print(&f2_op, "INIT");
                break;
            case F2_DSK_RWC:
                string_buffer_print(&f2_op, "RWC");
                break;
            case F2_DSK_RECNO:
                string_buffer_print(&f2_op, "RECNO");
                break;
            case F2_DSK_XFRDAT:
                string_buffer_print(&f2_op, "XFRDAT");
                break;
            case F2_DSK_SWRNRDY:
                string_buffer_print(&f2_op, "SWRNRDY");
                break;
            case F2_DSK_NFER:
                string_buffer_print(&f2_op, "NFER");
                break;
            case F2_DSK_STROBON:
                string_buffer_print(&f2_op, "STROBON");
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
                string_buffer_print(&f2_op, "EOSFCT");
                break;
            case F2_ETH_ERBFCT:
                string_buffer_print(&f2_op, "ERBFCT");
                break;
            case F2_ETH_EEFCT:
                string_buffer_print(&f2_op, "EEFCT");
                break;
            case F2_ETH_EBFCT:
                string_buffer_print(&f2_op, "EBFCT");
                break;
            case F2_ETH_ECBFCT:
                string_buffer_print(&f2_op, "ECBFCT");
                break;
            case F2_ETH_EISFCT:
                string_buffer_print(&f2_op, "EISFCT");
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
                string_buffer_print(&f2_op, "EVENFIELD");
                break;
            case F2_DH_SETMODE:
                string_buffer_print(&f2_op, "SETMODE");
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
                string_buffer_print(&f2_op, "EVENFIELD");
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

    string_buffer_print(output, "%s%s%s%s",
                        f1_buffer,
                        (f1_buffer[0] && f2_buffer[0]) ? ", " : "",
                        f2_buffer,
                        (f1_buffer[0] || f2_buffer[0]) ? ", " : "");
}

/* Decodes the bus RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_bus_rhs(struct decoder *dec,
                    struct string_buffer *output)
{
    if (dec->mc.use_constant) {
        /* Decode the constant. */
        (*dec->const_cb)(dec, dec->mc.const_addr, output);
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
        (*dec->reg_cb)(dec, dec->mc.rsel, output);
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
                    (*dec->reg_cb)(dec, dec->mc.rsel | (R_MASK + 1),
                                   output);
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
 * The output string is placed in `output`.
 */
static
void decode_bus_lhs(struct decoder *dec, int force,
                    struct string_buffer *output)
{
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

/* Decodes the bus assignments..
 * The output string is placed in `output`.
 */
static
void decode_bus_assign(struct decoder *dec,
                       struct string_buffer *output)
{
    size_t len;
    len = output->len;
    decode_bus_lhs(dec, FALSE, output);
    dec->has_bus_assignment = (len != output->len);
    if (!dec->has_bus_assignment) return;
    decode_bus_rhs(dec, output);
    string_buffer_print(output, ", ");
}

/* Decodes the alu RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_alu_rhs(struct decoder *dec,
                    struct string_buffer *output)
{
    if (dec->mc.aluf != ALU_T) {
        decode_bus_rhs(dec, output);
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

/* Decodes the alu LHS (destinations).
 * The output string is placed in `output`.
 */
static
void decode_alu_lhs(struct decoder *dec,
                    struct string_buffer *output)
{
    if (dec->mc.aluf == ALU_BUS) {
        decode_bus_lhs(dec, TRUE, output);
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

/* Decodes the alu assignments..
 * The output string is placed in `output`.
 */
static
void decode_alu_assign(struct decoder *dec,
                       struct string_buffer *output)
{
    size_t len;
    len = output->len;
    decode_alu_lhs(dec, output);
    dec->has_alu_assignment = (len != output->len);
    if (!dec->has_alu_assignment) return;
    decode_alu_rhs(dec, output);
    string_buffer_print(output, ", ");
}

/* Decodes the L register RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_lreg_rhs(struct decoder *dec,
                     struct string_buffer *output)
{
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

/* Decodes the L register LHS (destinations).
 * The output string is placed in `output`.
 */
static
void decode_lreg_lhs(struct decoder *dec,
                     struct string_buffer *output)
{
    if ((!dec->mc.use_constant) && dec->mc.bs == BS_LOAD_R) {
        (*dec->reg_cb)(dec, dec->mc.rsel, output);
        string_buffer_print(output, "<- ");
    }
}

/* Decodes assigments from the L register.
 * The output string is placed in `output`.
 */
static
void decode_lreg_assign(struct decoder *dec,
                        struct string_buffer *output)
{
    size_t len;
    len = output->len;
    decode_lreg_lhs(dec, output);
    if (len == output->len) return;
    decode_lreg_rhs(dec, output);
    string_buffer_print(output, ", ");
}

/* Decodes the M register RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_mreg_rhs(struct decoder *dec,
                     struct string_buffer *output)
{
    UNUSED(dec);
    string_buffer_print(output, "M");
}

/* Decodes the M register LHS (destinations).
 * The output string is placed in `output`.
 */
static
void decode_mreg_lhs(struct decoder *dec,
                     struct string_buffer *output)
{
    if ((!dec->mc.use_constant) && dec->mc.ram_task
        && dec->mc.bs == BS_RAM_LOAD_S_LOCATION) {

        (*dec->reg_cb)(dec, dec->mc.rsel | (R_MASK + 1), output);
        string_buffer_print(output, "<- ");
    }
}

/* Decodes assigments from the M register.
 * The output string is placed in `output`.
 */
static
void decode_mreg_assign(struct decoder *dec,
                        struct string_buffer *output)
{
    size_t len;
    len = output->len;
    decode_mreg_lhs(dec, output);
    if (len == output->len) return;
    decode_mreg_rhs(dec, output);
    string_buffer_print(output, ", ");
}

/* Decodes the SINK (bus) register destinations.
 * The output string is placed in `output`.
 */
static
void decode_sink_bus_lhs(struct decoder *dec,
                         struct string_buffer *output)
{
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

/* Decodes assigments to the SINK register (for bus).
 * The output string is placed in `output`.
 */
static
void decode_sink_bus_assign(struct decoder *dec,
                            struct string_buffer *output)
{
    size_t len;
    len = output->len;
    decode_sink_bus_lhs(dec, output);
    if (len == output->len) return;
    decode_bus_rhs(dec, output);
    string_buffer_print(output, ", ");
}

/* Decodes the SINK RHS (source) (for constants).
 * The output string is placed in `output`.
 */
static
void decode_sink_const_rhs(struct decoder *dec,
                           struct string_buffer *output)
{
    (dec->reg_cb)(dec, dec->mc.rsel, output);
}

/* Decodes the SINK RHS (destinations) (for constants).
 * The output string is placed in `output`.
 */
static
void decode_sink_const_lhs(struct decoder *dec,
                           struct string_buffer *output)
{
    if (dec->mc.use_constant) return;
    if (!dec->mc.bs_use_crom) return;
    if (dec->mc.rsel == 0) return;

    string_buffer_print(output, "SINK<- ");
}

/* Decodes assigments to the SINK register (for constants).
 * The output string is placed in `output`.
 */
static
void decode_sink_const_assign(struct decoder *dec,
                              struct string_buffer *output)
{
    size_t len;
    len = output->len;
    decode_sink_const_lhs(dec, output);
    if (len == output->len) return;
    decode_sink_const_rhs(dec, output);
    string_buffer_print(output, ", ");
}

/* Decodes the GOTO part of the instruction.
 * The output string is placed in `output`.
 * The original length of the `output` buffer (at the begining of
 * decoder_decode()) is in `orig_len`.
 */
static
void decode_goto(struct decoder *dec,
                 struct string_buffer *output,
                 size_t orig_len)
{
    size_t len;
    len = output->len;
    (*dec->goto_cb)(dec, dec->mc.next, output);
    if (len == output->len && len != orig_len) {
        /* Rewinds the ", " at the end of the string. */
        string_buffer_rewind(output, 2);
        return;
    }
}

void decoder_decode(struct decoder *dec,
                    const struct microcode *mc,
                    struct string_buffer *output)
{
    size_t len;

    dec->mc = *mc;
    dec->error = FALSE;
    dec->has_bus_assignment = FALSE;
    dec->has_alu_assignment = FALSE;

    len = output->len;

    decode_nondata_function(dec, output);
    if (dec->error) goto decode_error;

    decode_bus_assign(dec, output);
    if (dec->error) goto decode_error;

    decode_alu_assign(dec, output);
    if (dec->error) goto decode_error;

    decode_lreg_assign(dec, output);
    if (dec->error) goto decode_error;

    decode_mreg_assign(dec, output);
    if (dec->error) goto decode_error;

    decode_sink_bus_assign(dec, output);
    if (dec->error) goto decode_error;

    decode_sink_const_assign(dec, output);
    if (dec->error) goto decode_error;

    decode_goto(dec, output, len);
    if (dec->error) goto decode_error;
    return;

decode_error:
    if (output->len > len) {
        string_buffer_rewind(output, output->len - len);
    }
    string_buffer_print(output, "<invalid>");
}

