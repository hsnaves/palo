#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "microcode/microcode.h"
#include "common/utils.h"

/* Functions. */

void decode_buffer_reset(struct decode_buffer *buf)
{
    buf->pos = 0;
    buf->len = 0;
}

void decode_buffer_print(struct decode_buffer *buf,
                         const char *fmt, ...)
{
    int len;
    size_t l;
    va_list ap;

    va_start(ap, fmt);
    len = vsnprintf(&buf->buf[buf->pos],
                    buf->buf_size - buf->pos,
                    fmt, ap);
    va_end(ap);

    if (len <= 0) return;

    l = (size_t) len;
    if (buf->pos + l + 1 >= buf->buf_size) {
        buf->pos = buf->buf_size - 1;
    } else {
        buf->pos += l;
    }
    buf->len += len;
}

/* Decodes the non-data function part of the instruction.
 * The output string is placed in `output`.
 */
static
void decode_nondata_function(const struct decoder *dec,
                             struct decode_buffer *output)
{
    char f1_buffer[20];
    char f2_buffer[20];
    struct decode_buffer f1_op;
    struct decode_buffer f2_op;
    uint32_t microcode;
    uint16_t f1, f2;
    uint8_t task;

    microcode = dec->microcode;
    task = dec->task;

    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    f1_op.buf = f1_buffer;
    f1_op.buf_size = sizeof(f1_buffer);
    decode_buffer_reset(&f1_op);

    f2_op.buf = f2_buffer;
    f2_op.buf_size = sizeof(f2_buffer);
    decode_buffer_reset(&f2_op);

    f1_buffer[0] = '\0';
    switch (f1) {
    case F1_NONE:
    case F1_LOAD_MAR:
    case F1_LLSH1:
    case F1_LRSH1:
    case F1_LLCY8:
    case F1_CONSTANT:
        break;
    case F1_TASK:
        decode_buffer_print(&f1_op, "TASK");
        break;
    case F1_BLOCK:
        decode_buffer_print(&f1_op, "BLOCK");
        break;
    default:
        switch (task) {
        case TASK_EMULATOR:
            switch (f1) {
            case F1_EMU_SWMODE:
                decode_buffer_print(&f1_op, "SWMODE");
                break;
            case F1_EMU_WRTRAM:
                decode_buffer_print(&f1_op, "WRTRAM");
                break;
            case F1_EMU_RDRAM:
                decode_buffer_print(&f1_op, "RDRAM");
                break;
            case F1_EMU_STARTF:
                decode_buffer_print(&f1_op, "STARTF");
                break;
            default:
                break;
            }
            break;
        case TASK_DISK_SECTOR:
        case TASK_DISK_WORD:
            switch (f1) {
            case F1_DSK_STROBE:
                decode_buffer_print(&f1_op, "STROBE");
                break;
            case F1_DSK_INCRECNO:
                decode_buffer_print(&f1_op, "INCRECNO");
                break;
            case F1_DSK_CLRSTAT:
                decode_buffer_print(&f1_op, "CLRSTAT");
                break;
            default:
                break;
            }
            break;
        case TASK_ETHERNET:
            switch (f1) {
            case F1_ETH_EILFCT:
                decode_buffer_print(&f1_op, "EILFCT");
                break;
            case F1_ETH_EPFCT:
                decode_buffer_print(&f1_op, "EPFCT");
                break;
            case F1_ETH_EWFCT:
                decode_buffer_print(&f1_op, "EWFCT");
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        break;
    }

    f2_buffer[0] = '\0';
    switch (f2) {
    case F2_NONE:
    case F2_STORE_MD:
    case F2_CONSTANT:
        break;
    case F2_BUSEQ0:
        decode_buffer_print(&f2_op, "BUS=0");
        break;
    case F2_SHLT0:
        decode_buffer_print(&f2_op, "SH<0");
        break;
    case F2_SHEQ0:
        decode_buffer_print(&f2_op, "SH=0");
        break;
    case F2_BUS:
        decode_buffer_print(&f2_op, "BUS");
        break;
    case F2_ALUCY:
        decode_buffer_print(&f2_op, "ALUCY");
        break;
    default:
        switch (task) {
        case TASK_EMULATOR:
            switch (f2) {
            case F2_EMU_BUSODD:
                decode_buffer_print(&f2_op, "BUSODD");
                break;
            case F2_EMU_IDISP:
                decode_buffer_print(&f2_op, "IDISP");
                break;
            default:
                break;
            }
            break;
        case TASK_DISK_SECTOR:
        case TASK_DISK_WORD:
            switch (f2) {
            case F2_DSK_INIT:
                decode_buffer_print(&f2_op, "INIT");
                break;
            case F2_DSK_RWC:
                decode_buffer_print(&f2_op, "RWC");
                break;
            case F2_DSK_RECNO:
                decode_buffer_print(&f2_op, "RECNO");
                break;
            case F2_DSK_XFRDAT:
                decode_buffer_print(&f2_op, "XFRDAT");
                break;
            case F2_DSK_SWRNRDY:
                decode_buffer_print(&f2_op, "SWRNRDY");
                break;
            case F2_DSK_NFER:
                decode_buffer_print(&f2_op, "NFER");
                break;
            case F2_DSK_STROBON:
                decode_buffer_print(&f2_op, "STROBON");
                break;
            default:
                break;
            }
            break;
        case TASK_ETHERNET:
            switch (f2) {
            case F2_ETH_EOSFCT:
                decode_buffer_print(&f2_op, "EOSFCT");
                break;
            case F2_ETH_ERBFCT:
                decode_buffer_print(&f2_op, "ERBFCT");
                break;
            case F2_ETH_EEFCT:
                decode_buffer_print(&f2_op, "EEFCT");
                break;
            case F2_ETH_EBFCT:
                decode_buffer_print(&f2_op, "EBFCT");
                break;
            case F2_ETH_ECBFCT:
                decode_buffer_print(&f2_op, "ECBFCT");
                break;
            case F2_ETH_EISFCT:
                decode_buffer_print(&f2_op, "EISFCT");
                break;
            default:
                break;
            }
            break;
        case TASK_DISPLAY_WORD:
            break;
        case TASK_CURSOR:
            break;
        case TASK_DISPLAY_HORIZONTAL:
            switch (f2) {
            case F2_DH_EVENFIELD:
                decode_buffer_print(&f2_op, "EVENFIELD");
                break;
            case F2_DH_SETMODE:
                decode_buffer_print(&f2_op, "SETMODE");
                break;
            default:
                break;
            }
            break;
        case TASK_DISPLAY_VERTICAL:
            switch (f2) {
            case F2_DV_EVENFIELD:
                decode_buffer_print(&f2_op, "EVENFIELD");
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        break;
    }

    decode_buffer_print(output, "%s%s%s",
                        f1_buffer,
                        (f1_buffer[0] && f2_buffer[0]) ? ", " : "",
                        f2_buffer);
}

/* Decodes the bus RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_bus_rhs(const struct decoder *dec,
                    struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;
    uint16_t const_idx;
    uint8_t task;

    microcode = dec->microcode;
    task = dec->task;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT) {
        const_idx = CONST_ADDR(rsel, bs);
        /* Decode the constant. */
        (*dec->const_cb)(dec, const_idx, output);
        return;
    }

    switch (bs) {
    case BS_READ_R:
        if (task == TASK_EMULATOR && rsel == 0) {
            if (f2 == F2_EMU_ACDEST) {
                decode_buffer_print(output, "ACDEST");
                break;
            } else if (f2 == F2_EMU_ACSOURCE) {
                decode_buffer_print(output, "SOURCE");
                break;
            }
        }
        (*dec->reg_cb)(dec, rsel, output);
        break;
    case BS_LOAD_R:
        decode_buffer_print(output, "0");
        break;
    case BS_NONE:
        if (task == TASK_EMULATOR && f1 == F1_EMU_RSNF) {
            decode_buffer_print(output, "RSNF");
        } else if (task == TASK_ETHERNET && f1 == F1_ETH_EILFCT) {
            decode_buffer_print(output, "EILFCT");
        } else if (task == TASK_ETHERNET && f1 == F1_ETH_EPFCT) {
            decode_buffer_print(output, "EPFCT");
        } else {
            decode_buffer_print(output, "-1");
        }
        break;
    case BS_READ_MD:
        decode_buffer_print(output, "MD");
        break;
    case BS_READ_MOUSE:
        decode_buffer_print(output, "MOUSE");
        break;
    case BS_READ_DISP:
        decode_buffer_print(output, "DISP");
        break;
    default:
        if (task == TASK_ETHERNET && bs == BS_ETH_EIDFCT) {
            decode_buffer_print(output, "EIDFCT");
        } else if ((task == TASK_DISK_SECTOR) || (task == TASK_DISK_WORD)) {
            if (bs == BS_DSK_READ_KSTAT) {
                decode_buffer_print(output, "KSTAT");
            } else if (bs == BS_DSK_READ_KDATA) {
                decode_buffer_print(output, "KDATA");
            }
        }
        break;
    }
}

/* Decodes the bus LHS (destinations).
 * If `force` parameter is set to TRUE, the output is produced
 * even when the ALUF is ALU_BUS.
 * The output string is placed in `output`.
 */
static
void decode_bus_lhs(const struct decoder *dec, int force,
                    struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t f1, f2;
    uint16_t aluf;
    uint8_t task;
    int load_t;
    int load_t_from_alu;

    microcode = dec->microcode;
    task = dec->task;

    aluf = MICROCODE_ALUF(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);
    load_t = MICROCODE_T(microcode);

    load_t_from_alu = LOAD_T_FROM_ALU(aluf);

    /* If ALUF is BUS, then we skip the BUS assignments, and
     * merge them with the ALU assigments.
     */
    if (aluf == ALU_BUS && !force) return;

    if (load_t && (!load_t_from_alu)) {
        decode_buffer_print(output, "T<- ");
    }

    switch (task) {
    case TASK_EMULATOR:
        switch (f1) {
        case F1_EMU_LOAD_RMR:
            decode_buffer_print(output, "RMR<- ");
            break;
        case F1_EMU_LOAD_ESRB:
            decode_buffer_print(output, "ESRB<- ");
            break;
        default:
            break;
        }
        break;
    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (f1) {
        case F1_DSK_LOAD_KSTAT:
            decode_buffer_print(output, "KSTAT<- ");
            break;
        case F1_DSK_LOAD_KCOMM:
            decode_buffer_print(output, "KCOMM<- ");
            break;
        case F1_DSK_LOAD_KADR:
            decode_buffer_print(output, "KADR<- ");
            break;
        case F1_DSK_LOAD_KDATA:
            decode_buffer_print(output, "KDATA<- ");
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    switch (f2) {
    case F2_STORE_MD:
        if (f1 != F1_LOAD_MAR) {
            decode_buffer_print(output, "MD<- ");
        }
        break;
    default:
        switch (task) {
        case TASK_EMULATOR:
            switch (f2) {
            case F2_EMU_LOAD_DNS:
                decode_buffer_print(output, "DNS<- ");
                break;
            case F2_EMU_LOAD_IR:
                decode_buffer_print(output, "IR<- ");
                break;
            default:
                break;
            }
            break;
        case TASK_ETHERNET:
            switch (f2) {
            case F2_ETH_EODFCT:
                decode_buffer_print(output, "EODFCT<- ");
                break;
            default:
                break;
            }
            break;
        case TASK_DISPLAY_WORD:
            switch (f2) {
            case F2_DW_LOAD_DDR:
                decode_buffer_print(output, "DDR<- ");
                break;
            default:
                break;
            }
            break;
        case TASK_CURSOR:
            switch (f2) {
            case F2_CUR_LOAD_XPREG:
                decode_buffer_print(output, "XPREG<- ");
                break;
            case F2_CUR_LOAD_CSR:
                decode_buffer_print(output, "CSR<- ");
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

/* Decodes the bus assignments..
 * The output string is placed in `output`.
 */
static
void decode_bus_assign(const struct decoder *dec,
                       struct decode_buffer *output)
{
    size_t len;
    len = output->len;
    decode_bus_lhs(dec, FALSE, output);
    if (len == output->len) return;
    decode_bus_rhs(dec, output);
}

/* Decodes the alu RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_alu_rhs(const struct decoder *dec,
                    struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t aluf;

    microcode = dec->microcode;

    aluf = MICROCODE_ALUF(microcode);

    if (aluf != ALU_T) {
        decode_bus_rhs(dec, output);
    }

    switch (aluf) {
    case ALU_BUS:
        break;
    case ALU_T:
        decode_buffer_print(output, "T");
        break;
    case ALU_BUS_OR_T:
        decode_buffer_print(output, " OR T");
        break;
    case ALU_BUS_AND_T:
        decode_buffer_print(output, " AND T");
        break;
    case ALU_BUS_XOR_T:
        decode_buffer_print(output, " XOR T");
        break;
    case ALU_BUS_PLUS_1:
        decode_buffer_print(output, " + 1");
        break;
    case ALU_BUS_MINUS_1:
        decode_buffer_print(output, " - 1");
        break;
    case ALU_BUS_PLUS_T:
        decode_buffer_print(output, " + T");
        break;
    case ALU_BUS_MINUS_T:
        decode_buffer_print(output, " - T");
        break;
    case ALU_BUS_MINUS_T_MINUS_1:
        decode_buffer_print(output, " - T - 1");
        break;
    case ALU_BUS_PLUS_T_PLUS_1:
        decode_buffer_print(output, " + T + 1");
        break;
    case ALU_BUS_PLUS_SKIP:
        decode_buffer_print(output, " + SKIP");
        break;
    case ALU_BUS_AND_T_WB:
        decode_buffer_print(output, " . T");
        break;
    case ALU_BUS_AND_NOT_T:
        decode_buffer_print(output, " AND NOT T");
        break;
    }
}

/* Decodes the alu LHS (destinations).
 * The output string is placed in `output`.
 */
static
void decode_alu_lhs(const struct decoder *dec,
                    struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t aluf;
    uint16_t f1, f2;
    int load_t, load_l;
    int load_t_from_alu;

    microcode = dec->microcode;

    aluf = MICROCODE_ALUF(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);
    load_t = MICROCODE_T(microcode);
    load_l = MICROCODE_L(microcode);

    load_t_from_alu = LOAD_T_FROM_ALU(aluf);

    if (aluf == ALU_BUS) {
        decode_bus_lhs(dec, TRUE, output);
    }

    if (load_t && (load_t_from_alu)) {
        decode_buffer_print(output, "T<- ");
    }

    if (load_l) {
        decode_buffer_print(output, "L<- ");
    }

    if (f1 == F1_LOAD_MAR) {
        if (f2 == F2_STORE_MD) {
            decode_buffer_print(output, "XMAR<- ");
        } else {
            decode_buffer_print(output, "MAR<- ");
        }
    }
}

/* Decodes the alu assignments..
 * The output string is placed in `output`.
 */
static
void decode_alu_assign(const struct decoder *dec,
                       struct decode_buffer *output)
{
    size_t len;
    len = output->len;
    decode_alu_lhs(dec, output);
    if (len == output->len) return;
    decode_alu_rhs(dec, output);
}

/* Decodes the L register RHS (source).
 * The output string is placed in `output`.
 */
static
void decode_lreg_rhs(const struct decoder *dec,
                     struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t f1, f2;

    microcode = dec->microcode;

    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    switch (f1) {
    case F1_LLSH1:
        if (f2 == F2_EMU_MAGIC) {
            decode_buffer_print(output, "L MLSH 1");
        } else {
            decode_buffer_print(output, "L LSH 1");
        }
        break;
    case F1_LRSH1:
        if (f2 == F2_EMU_MAGIC) {
            decode_buffer_print(output, "L MRSH 1");
        } else {
            decode_buffer_print(output, "L RSH 1");
        }
        break;
    case F1_LLCY8:
        decode_buffer_print(output, "L LCY 8");
        break;
    default:
        decode_buffer_print(output, "L");
        break;
    }
}

/* Decodes the L register LHS (destinations).
 * The output string is placed in `output`.
 */
static
void decode_lreg_lhs(const struct decoder *dec,
                     struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;
    int use_constant;

    microcode = dec->microcode;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    use_constant = (f1 == F1_CONSTANT || f2 == F2_CONSTANT
                    || BS_USE_CROM(bs));

    if (bs == BS_LOAD_R && (!use_constant)) {
        (*dec->reg_cb)(dec, rsel, output);
        decode_buffer_print(output, "<- ");
    }
}

/* Decodes assigments from the L register.
 * The output string is placed in `output`.
 */
static
void decode_lreg_assign(const struct decoder *dec,
                        struct decode_buffer *output)
{
    size_t len;
    len = output->len;
    decode_lreg_lhs(dec, output);
    if (len == output->len) return;
    decode_lreg_rhs(dec, output);
}

/* Decodes the SINK (bus) register destinations.
 * The output string is placed in `output`.
 */
static
void decode_sink_bus_lhs(const struct decoder *dec,
                         struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t rsel;
    uint16_t aluf;
    uint16_t bs;
    uint16_t f1, f2;
    uint8_t task;
    struct decode_buffer tbuf;
    char tbuffer[1];

    microcode = dec->microcode;
    task = dec->task;

    rsel = MICROCODE_RSEL(microcode);
    aluf = MICROCODE_ALUF(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    tbuf.buf = tbuffer;
    tbuf.buf_size = sizeof(tbuffer);
    decode_buffer_reset(&tbuf);

    decode_bus_lhs(dec, FALSE, &tbuf);
    if (tbuf.len != 0) return;

    if (aluf != ALU_T) {
        decode_alu_lhs(dec, &tbuf);
        if (tbuf.len != 0) return;
    }

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT)
        goto do_sink;

    switch (bs) {
    case BS_READ_R:
        if (rsel != 0) break;
        if (task != TASK_EMULATOR)
            return;
        if (f2 != F2_EMU_ACDEST && f2 != F2_EMU_ACSOURCE)
            return;
        break;
    case BS_LOAD_R:
        return;
    case BS_NONE:
        if (task == TASK_EMULATOR && f1 == F1_EMU_RSNF)
            break;
        else if (task == TASK_ETHERNET && f1 == F1_ETH_EILFCT)
            break;
        else if (task == TASK_ETHERNET && f1 == F1_ETH_EPFCT)
            break;
        return;
    case BS_READ_MD:
    case BS_READ_MOUSE:
    case BS_READ_DISP:
        break;
    default:
        if (task == TASK_ETHERNET && bs == BS_ETH_EIDFCT)
            break;

        if ((task == TASK_DISK_SECTOR) || (task == TASK_DISK_WORD)) {
            if (bs == BS_DSK_READ_KSTAT) {
                break;
            } else if (bs == BS_DSK_READ_KDATA) {
                break;
            }
        }
        return;
    }

do_sink:
    decode_buffer_print(output, "SINK<- ");
}

/* Decodes assigments to the SINK register (for bus).
 * The output string is placed in `output`.
 */
static
void decode_sink_bus_assign(const struct decoder *dec,
                            struct decode_buffer *output)
{
    size_t len;
    len = output->len;
    decode_sink_bus_lhs(dec, output);
    if (len == output->len) return;
    decode_bus_rhs(dec, output);
}

/* Decodes the SINK RHS (source) (for constants).
 * The output string is placed in `output`.
 */
static
void decode_sink_const_rhs(const struct decoder *dec,
                           struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t rsel;

    microcode = dec->microcode;
    rsel = MICROCODE_RSEL(microcode);

    (dec->reg_cb)(dec, rsel, output);
}

/* Decodes the SINK RHS (destinations) (for constants).
 * The output string is placed in `output`.
 */
static
void decode_sink_const_lhs(const struct decoder *dec,
                           struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;

    microcode = dec->microcode;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT)
        return;

    if (!BS_USE_CROM(bs)) return;

    if (rsel == 0) return;

    decode_buffer_print(output, "SINK<- ");
}

/* Decodes assigments to the SINK register (for constants).
 * The output string is placed in `output`.
 */
static
void decode_sink_const_assign(const struct decoder *dec,
                              struct decode_buffer *output)
{
    size_t len;
    len = output->len;
    decode_sink_const_lhs(dec, output);
    if (len == output->len) return;
    decode_sink_const_rhs(dec, output);
}

/* Decodes the GOTO part of the instruction.
 * The output string is placed in `output`.
 */
static
void decode_goto(const struct decoder *dec,
                 struct decode_buffer *output)
{
    uint32_t microcode;
    uint16_t next;

    microcode = dec->microcode;
    next = MICROCODE_NEXT(microcode);

    (*dec->goto_cb)(dec, next, output);
}

void decoder_decode(const struct decoder *dec,
                    struct decode_buffer *output)
{
    struct decode_buffer tbuf;
    char tbuffer[1];
    int has_something;

    tbuf.buf = tbuffer;
    tbuf.buf_size = sizeof(tbuffer);
    decode_buffer_reset(&tbuf);

    decode_buffer_reset(&tbuf);
    decode_nondata_function(dec, &tbuf);
    has_something = (tbuf.len != 0);

    decode_nondata_function(dec, output);

    /* Prints the bus assignments. */
    decode_buffer_reset(&tbuf);
    decode_bus_assign(dec, &tbuf);

    if (has_something && (tbuf.len != 0)) {
        decode_buffer_print(output, ", ");
    }
    has_something = has_something || (tbuf.len != 0);
    decode_bus_assign(dec, output);

    /* Prints the alu assignments. */
    decode_buffer_reset(&tbuf);
    decode_alu_assign(dec, &tbuf);

    if (has_something && (tbuf.len != 0)) {
        decode_buffer_print(output, ", ");
    }
    has_something = has_something || (tbuf.len != 0);
    decode_alu_assign(dec, output);

    /* Prints the L register assignments. */
    decode_buffer_reset(&tbuf);
    decode_lreg_assign(dec, &tbuf);

    if (has_something && (tbuf.len != 0)) {
        decode_buffer_print(output, ", ");
    }
    has_something = has_something || (tbuf.len != 0);
    decode_lreg_assign(dec, output);

    /* Prints the SINK (bus) assignments. */
    decode_buffer_reset(&tbuf);
    decode_sink_bus_assign(dec, &tbuf);

    if (has_something && (tbuf.len != 0)) {
        decode_buffer_print(output, ", ");
    }
    has_something = has_something || (tbuf.len != 0);
    decode_sink_bus_assign(dec, output);

    /* Prints the SINK (const) assignments. */
    decode_buffer_reset(&tbuf);
    decode_sink_const_assign(dec, &tbuf);

    if (has_something && (tbuf.len != 0)) {
        decode_buffer_print(output, ", ");
    }
    has_something = has_something || (tbuf.len != 0);
    decode_sink_const_assign(dec, output);

    /* Prints the GOTO. */
    decode_buffer_reset(&tbuf);
    decode_goto(dec, &tbuf);

    if (has_something && (tbuf.len != 0)) {
        decode_buffer_print(output, ", ");
    }
    has_something = has_something || (tbuf.len != 0);
    decode_goto(dec, output);
}

