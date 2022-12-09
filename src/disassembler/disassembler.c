#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "disassembler/disassembler.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Special bits. */
#define SPECIAL_DSK_INIT       0x400

/* Other constants. */
#define MARK_VISITED               1
#define MARK_PENDING               2

/* Functions. */

void disassembler_initvar(struct disassembler *dis)
{
    dis->consts = NULL;
    dis->microcode = NULL;
    dis->task_mask = NULL;
    dis->next_mask = NULL;
    dis->hint_mask = NULL;
    dis->stack = NULL;
    dis->mark = NULL;
}

void disassembler_destroy(struct disassembler *dis)
{
    if (dis->consts) free((void *) dis->consts);
    dis->consts = NULL;

    if (dis->microcode) free((void *) dis->microcode);
    dis->microcode = NULL;

    if (dis->task_mask) free((void *) dis->task_mask);
    dis->task_mask = NULL;

    if (dis->next_mask) free((void *) dis->next_mask);
    dis->next_mask = NULL;

    if (dis->hint_mask) free((void *) dis->hint_mask);
    dis->hint_mask = NULL;

    if (dis->stack) free((void *) dis->stack);
    dis->stack = NULL;

    if (dis->mark) free((void *) dis->mark);
    dis->mark = NULL;
}

int disassembler_create(struct disassembler *dis)
{
    disassembler_initvar(dis);

    dis->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    dis->microcode = (uint32_t *)
        malloc(MICROCODE_SIZE * sizeof(uint32_t));
    dis->task_mask = (uint16_t *)
        malloc(MICROCODE_SIZE * sizeof(uint16_t));
    dis->next_mask = (uint16_t *)
        malloc(MICROCODE_SIZE * sizeof(uint16_t));
    dis->hint_mask = (uint16_t *)
        malloc(MICROCODE_SIZE * sizeof(uint16_t));
    dis->stack = (uint16_t *)
        malloc(MICROCODE_SIZE * sizeof(uint16_t));
    dis->mark = (uint8_t *)
        malloc(MICROCODE_SIZE * sizeof(uint8_t));

    if (unlikely(!dis->consts || !dis->microcode
                 || !dis->task_mask || !dis->next_mask
                 || !dis->hint_mask
                 || !dis->stack || !dis->mark)) {
        report_error("disassembler: create: "
                     "memory exhausted");
        disassembler_destroy(dis);
        return FALSE;
    }
    return TRUE;
}

int disassembler_load_constant_rom(struct disassembler *dis,
                                   const char *filename)
{
    FILE *fp;
    uint16_t i;
    uint16_t val;
    int c;

    if (!filename) return TRUE;
    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("disassembler: load_constant_rom: "
                     "cannot open `%s`", filename);
        return FALSE;
    }

    for (i = 0; i < CONSTANT_SIZE; i++) {
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val = (uint16_t) (c & 0xFF);

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint16_t) (c & 0xFF)) << 8;

        dis->consts[i] = val;
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("disassembler: load_constant_rom: "
                     "invalid file size `%s`",
                     filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("disassembler: load_constant_rom: "
                 "premature end of file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int disassembler_load_microcode_rom(struct disassembler *dis,
                                    const char *filename)
{
    FILE *fp;
    uint16_t i;
    uint32_t val;
    int c;

    if (!filename) return TRUE;
    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("disassembler: load_microcode_rom: "
                     "cannot open `%s`", filename);
        return FALSE;
    }

    for (i = 0; i < MICROCODE_SIZE; i++) {
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val = (uint32_t) (c & 0xFF);

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 8;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 16;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 24;

        dis->microcode[i] = val;
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("disassembler: load_microcode_rom: "
                     "invalid file size `%s`",
                     filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("disassembler: load_microcode_rom: "
                 "premature end of file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

/* Updates the next_mask of a given microcode address.
 * This will add the address to the stack if any modification is performed.
 */
static
void update_address_info(struct disassembler *dis, uint16_t i,
                         uint16_t next_mask)
{
    int modified;

    modified = (!(dis->mark[i] & MARK_VISITED));
    modified = modified || ((dis->next_mask[i] & next_mask) != next_mask);
    if (!modified) return;

    dis->next_mask[i] |= next_mask;

    if (!(dis->mark[i] & MARK_PENDING)) {
        /* Add it to the stack. */
        dis->stack[dis->stack_top++] = i;
        dis->mark[i] |= MARK_PENDING;
    }
}

/* Propagates the addresses of a given task.
 * This function will try to figure out which addresses correspond to
 * which tasks.
 * The current task being analyzed is given by `task`.
 */
static
void propagate_addresses(struct disassembler *dis, uint8_t task)
{
    uint16_t i, j, next;
    uint16_t rsel, f1, f2, bs;
    uint16_t next_mask;
    uint16_t following_next_mask;
    uint32_t microcode, following_microcode;

    while (dis->stack_top > 0) {
        /* Pop an element from the stack. */
        i = dis->stack[--(dis->stack_top)];
        dis->mark[i] = MARK_VISITED;

        next_mask = dis->next_mask[i];
        microcode = dis->microcode[i];

        rsel = MICROCODE_RSEL(microcode);
        bs = MICROCODE_BS(microcode);
        f1 = MICROCODE_F1(microcode);
        f2 = MICROCODE_F2(microcode);
        next = MICROCODE_NEXT(microcode);

        following_next_mask = 0;
        following_microcode = dis->microcode[next];

        switch (f2) {
        case F2_BUSEQ0: following_next_mask |= 0x1; break;
        case F2_SHLT0:  following_next_mask |= 0x1; break;
        case F2_SHEQ0:  following_next_mask |= 0x1; break;
        case F2_BUS:
            /* Need to compute a more acurate mask. */
            if (BS_USE_CROM(bs) || f1 == F1_CONSTANT) {
                following_next_mask |=
                    dis->hint_mask[MICROCODE_NEXT(following_microcode)]
                    & (dis->consts[CONST_ADDR(rsel, bs)]);
            } else {
                following_next_mask |=
                    dis->hint_mask[MICROCODE_NEXT(following_microcode)];
            }
            break;
        case F2_ALUCY:  following_next_mask |= 0x1; break;
        default: break;
        }

        switch (task) {
        case TASK_EMULATOR:
            switch (f2) {
            case F2_EMU_BUSODD:   following_next_mask |= 0x1; break;
            case F2_EMU_LOAD_IR:  following_next_mask |= 0xF; break;
            case F2_EMU_IDISP:    following_next_mask |= 0xF; break;
            case F2_EMU_ACSOURCE: following_next_mask |= 0xF; break;
            default: break;
            }
            break;
        case TASK_ETHERNET:
            switch (f2) {
            case F2_ETH_ERBFCT:   following_next_mask |= 0xC; break;
            case F2_ETH_EBFCT:    following_next_mask |= 0xC; break;
            case F2_ETH_ECBFCT:   following_next_mask |= 0x4; break;
            default: break;
            }
            break;
        case TASK_DISPLAY_HORIZONTAL:
            switch (f2) {
            case F2_DH_EVENFIELD: following_next_mask |= 0x1; break;
            case F2_DH_SETMODE:   following_next_mask |= 0x1; break;
            default: break;
            }
            break;
        case TASK_DISPLAY_VERTICAL:
            switch (f2) {
            case F2_DV_EVENFIELD: following_next_mask |= 0x1; break;
            default: break;
            }
            break;
        case TASK_DISK_WORD:
        case TASK_DISK_SECTOR:
            switch (f2) {
            case F2_DSK_INIT:
                following_next_mask |= SPECIAL_DSK_INIT;
                break;
            case F2_DSK_RWC:      following_next_mask |= 0x3; break;
            case F2_DSK_RECNO:    following_next_mask |= 0x3; break;
            case F2_DSK_XFRDAT:   following_next_mask |= 0x1; break;
            case F2_DSK_SWRNRDY:  following_next_mask |= 0x1; break;
            case F2_DSK_NFER:     following_next_mask |= 0x1; break;
            case F2_DSK_STROBON:  following_next_mask |= 0x1; break;
            default: break;
            }
            break;

        default:
            break;
        }


        if (next_mask & SPECIAL_DSK_INIT) {
            j = 0x1F;
            update_address_info(dis, next | j,
                                following_next_mask);
        }

        next_mask &= MICROCODE_SIZE - 1;
        for (j = 0; j <= next_mask; j++) {
            update_address_info(dis, next | (j & next_mask),
                                following_next_mask);
        }
    }
}

void disassembler_find_task_addresses(struct disassembler *dis)
{
    uint16_t i, task, task_mask;

    memset(dis->task_mask, 0, MICROCODE_SIZE * sizeof(uint16_t));

    for (i = 0; i < MICROCODE_SIZE; i++)
        dis->hint_mask[i] = MICROCODE_SIZE - 1;

    dis->hint_mask[0] = 017;
    dis->hint_mask[0100] = 037;
    dis->hint_mask[0160] = 037;
    dis->hint_mask[0340] = 017;
    dis->hint_mask[0440] = 017;
    dis->hint_mask[0460] = 07;
    dis->hint_mask[0600] = 07;
    dis->hint_mask[0607] = 01;
    dis->hint_mask[01160] = 017;
    dis->hint_mask[01434] = 03;
    dis->hint_mask[01443] = 017;
    dis->hint_mask[01463] = 017;
    dis->hint_mask[01474] = 03;
    dis->hint_mask[01515] = 02;

    task_mask = (1 << TASK_EMULATOR)
        | (1 << TASK_DISK_SECTOR)
        | (1 << TASK_ETHERNET)
        | (1 << TASK_MEMORY_REFRESH)
        | (1 << TASK_DISPLAY_WORD)
        | (1 << TASK_CURSOR)
        | (1 << TASK_DISPLAY_HORIZONTAL)
        | (1 << TASK_DISPLAY_VERTICAL)
        | (1 << TASK_PARITY)
        | (1 << TASK_DISK_WORD);

    for (task = 0; task < TASK_NUM_TASKS; task++) {
        if (!(task_mask & (1 << task))) continue;

        memset(dis->next_mask, 0, MICROCODE_SIZE * sizeof(uint16_t));
        memset(dis->mark, 0, MICROCODE_SIZE * sizeof(uint8_t));

        dis->stack_top = 0;
        dis->mark[task] = MARK_PENDING;
        dis->stack[dis->stack_top++] = task;

        propagate_addresses(dis, task);
        for (i = 0; i < MICROCODE_SIZE; i++) {
            if (dis->mark[i] & MARK_VISITED) {
                dis->task_mask[i] |= (1 << task);
            }
        }
    }
}

/* Helpful macro to update the buffer positions. */
#define UPDATE_BUFFER(buffer, buffer_size, ret) \
    if (((size_t) (ret)) < buffer_size) {       \
        buffer += ret;                          \
        buffer_size -= (size_t) (ret);          \
    } else {                                    \
        buffer += (buffer_size - 1);            \
        buffer_size = 1;                        \
    }

/* Disassembles the non-data function part of the instruction.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_nondata_function(struct disassembler *dis,
                            uint32_t microcode, uint8_t task,
                            char *buffer, size_t buffer_size)
{
    char f1_op[20];
    char f2_op[20];
    uint16_t f1, f2;
    int n_buffer;

    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    f1_op[0] = '\0';
    switch(f1) {
    case F1_NONE:
    case F1_LOAD_MAR:
    case F1_LLSH1:
    case F1_LRSH1:
    case F1_LLCY8:
    case F1_CONSTANT:
        break;
    case F1_TASK:
        snprintf(f1_op, sizeof(f1_op), "TASK");
        break;
    case F1_BLOCK:
        snprintf(f1_op, sizeof(f1_op), "BLOCK");
        break;
    default:
        switch(task) {
        case TASK_EMULATOR:
            switch(f1) {
            case F1_EMU_SWMODE:
                snprintf(f1_op, sizeof(f1_op), "SWMODE");
                break;
            case F1_EMU_WRTRAM:
                snprintf(f1_op, sizeof(f1_op), "WRTRAM");
                break;
            case F1_EMU_RDRAM:
                snprintf(f1_op, sizeof(f1_op), "RDRAM");
                break;
            case F1_EMU_STARTF:
                snprintf(f1_op, sizeof(f1_op), "STARTF");
                break;
            default:
                break;
            }
            break;
        case TASK_DISK_SECTOR:
        case TASK_DISK_WORD:
            switch(f1) {
            case F1_DSK_STROBE:
                snprintf(f1_op, sizeof(f1_op), "STROBE");
                break;
            case F1_DSK_INCRECNO:
                snprintf(f1_op, sizeof(f1_op), "INCRECNO");
                break;
            case F1_DSK_CLRSTAT:
                snprintf(f1_op, sizeof(f1_op), "CLRSTAT");
                break;
            default:
                break;
            }
            break;
        case TASK_ETHERNET:
            switch(f1) {
            case F1_ETH_EILFCT:
                snprintf(f1_op, sizeof(f1_op), "EILFCT");
                break;
            case F1_ETH_EPFCT:
                snprintf(f1_op, sizeof(f1_op), "EPFCT");
                break;
            case F1_ETH_EWFCT:
                snprintf(f1_op, sizeof(f1_op), "EWFCT");
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

    f2_op[0] = '\0';
    switch(f2) {
    case F2_NONE:
    case F2_STORE_MD:
    case F2_CONSTANT:
        break;
    case F2_BUSEQ0:
        snprintf(f2_op, sizeof(f2_op), "BUS=0");
        break;
    case F2_SHLT0:
        snprintf(f2_op, sizeof(f2_op), "SH<0");
        break;
    case F2_SHEQ0:
        snprintf(f2_op, sizeof(f2_op), "SH=0");
        break;
    case F2_BUS:
        snprintf(f2_op, sizeof(f2_op), "BUS");
        break;
    case F2_ALUCY:
        snprintf(f2_op, sizeof(f2_op), "ALUCY");
        break;
    default:
        switch(task) {
        case TASK_EMULATOR:
            switch(f2) {
            case F2_EMU_BUSODD:
                snprintf(f2_op, sizeof(f2_op), "BUSODD");
                break;
            case F2_EMU_IDISP:
                snprintf(f2_op, sizeof(f2_op), "IDISP");
                break;
            default:
                break;
            }
            break;
        case TASK_DISK_SECTOR:
        case TASK_DISK_WORD:
            switch(f2) {
            case F2_DSK_INIT:
                snprintf(f2_op, sizeof(f2_op), "INIT");
                break;
            case F2_DSK_RWC:
                snprintf(f2_op, sizeof(f2_op), "RWC");
                break;
            case F2_DSK_RECNO:
                snprintf(f2_op, sizeof(f2_op), "RECNO");
                break;
            case F2_DSK_XFRDAT:
                snprintf(f2_op, sizeof(f2_op), "XFRDAT");
                break;
            case F2_DSK_SWRNRDY:
                snprintf(f2_op, sizeof(f2_op), "SWRNRDY");
                break;
            case F2_DSK_NFER:
                snprintf(f2_op, sizeof(f2_op), "NFER");
                break;
            case F2_DSK_STROBON:
                snprintf(f2_op, sizeof(f2_op), "STROBON");
                break;
            default:
                break;
            }
            break;
        case TASK_ETHERNET:
            switch(f2) {
            case F2_ETH_EOSFCT:
                snprintf(f2_op, sizeof(f2_op), "EOSFCT");
                break;
            case F2_ETH_ERBFCT:
                snprintf(f2_op, sizeof(f2_op), "ERBFCT");
                break;
            case F2_ETH_EEFCT:
                snprintf(f2_op, sizeof(f2_op), "EEFCT");
                break;
            case F2_ETH_EBFCT:
                snprintf(f2_op, sizeof(f2_op), "EBFCT");
                break;
            case F2_ETH_ECBFCT:
                snprintf(f2_op, sizeof(f2_op), "ECBFCT");
                break;
            case F2_ETH_EISFCT:
                snprintf(f2_op, sizeof(f2_op), "EISFCT");
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
            switch(f2) {
            case F2_DH_EVENFIELD:
                snprintf(f2_op, sizeof(f2_op), "EVENFIELD");
                break;
            case F2_DH_SETMODE:
                snprintf(f2_op, sizeof(f2_op), "SETMODE");
                break;
            default:
                break;
            }
            break;
        case TASK_DISPLAY_VERTICAL:
            switch(f2) {
            case F2_DV_EVENFIELD:
                snprintf(f2_op, sizeof(f2_op), "EVENFIELD");
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

    n_buffer = snprintf(buffer, buffer_size, "%s%s%s",
                        f1_op,
                        (f1_op[0] && f2_op[0]) ? ", " : "",
                        f2_op);
    return n_buffer;
}

/* Disassembles the bus source.
 * Returns the number of character written in the buffer.
 */
static
int disasm_bus_rhs(struct disassembler *dis,
                   uint32_t microcode, uint8_t task,
                   char *buffer, size_t buffer_size)
{
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;
    int ret;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT) {
        return snprintf(buffer, buffer_size,
                        "%o", dis->consts[CONST_ADDR(rsel, bs)]);
    }

    switch (bs) {
    case BS_READ_R:
        if (task == TASK_EMULATOR && rsel == 0) {
            if (f2 == F2_EMU_ACDEST) {
                ret = snprintf(buffer, buffer_size, "ACDEST");
                break;
            } else if (f2 == F2_EMU_ACSOURCE) {
                ret = snprintf(buffer, buffer_size, "SOURCE");
                break;
            }
        }
        ret = snprintf(buffer, buffer_size, "R%o", rsel);
        break;
    case BS_LOAD_R:
        ret = snprintf(buffer, buffer_size, "0");
        break;
    case BS_NONE:
        if (task == TASK_EMULATOR && f1 == F1_EMU_RSNF) {
            ret = snprintf(buffer, buffer_size, "RSNF");
            break;
        } else if (task == TASK_ETHERNET && f1 == F1_ETH_EILFCT) {
            ret = snprintf(buffer, buffer_size, "EILFCT");
            break;
        } else if (task == TASK_ETHERNET && f1 == F1_ETH_EPFCT) {
            ret = snprintf(buffer, buffer_size, "EPFCT");
            break;
        }
        ret = snprintf(buffer, buffer_size, "-1");
        break;
    case BS_READ_MD:
        ret = snprintf(buffer, buffer_size, "MD");
        break;
    case BS_READ_MOUSE:
        ret = snprintf(buffer, buffer_size, "MOUSE");
        break;
    case BS_READ_DISP:
        ret = snprintf(buffer, buffer_size, "DISP");
        break;
    default:
        if (task == TASK_ETHERNET && bs == BS_ETH_EIDFCT) {
            ret = snprintf(buffer, buffer_size, "EIDFCT");
            break;
        }
        if ((task == TASK_DISK_SECTOR) || (task == TASK_DISK_WORD)) {
            if (bs == BS_DSK_READ_KSTAT) {
                ret = snprintf(buffer, buffer_size, "KSTAT");
                break;
            } else if (bs == BS_DSK_READ_KDATA) {
                ret = snprintf(buffer, buffer_size, "KDATA");
                break;
            }
        }
        ret = 0;
        break;
    }

    return ret;
}

/* Disassembles the bus destinations.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_bus_lhs(struct disassembler *dis,
                   uint32_t microcode, uint8_t task, int force,
                   char *buffer, size_t buffer_size)
{
    uint16_t f1, f2;
    uint16_t aluf;
    int load_t;
    int load_t_from_alu;
    int ret, n_buffer;

    aluf = MICROCODE_ALUF(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);
    load_t = MICROCODE_T(microcode);

    load_t_from_alu = LOAD_T_FROM_ALU(aluf);

    /* If ALUF is BUS, then we skip the BUS assignments, and
     * merge them with the ALU assigments.
     */
    if (aluf == ALU_BUS && !force) return 0;

    n_buffer = 0;

    if (load_t && (!load_t_from_alu)) {
        ret = snprintf(buffer, buffer_size, "T<- ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    switch (task) {
    case TASK_EMULATOR:
        switch (f1) {
        case F1_EMU_LOAD_RMR:
            ret = snprintf(buffer, buffer_size, "RMR<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
            break;
        case F1_EMU_LOAD_ESRB:
            ret = snprintf(buffer, buffer_size, "ESRB<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
            break;
        default:
            break;
        }
        break;
    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (f1) {
        case F1_DSK_LOAD_KSTAT:
            ret = snprintf(buffer, buffer_size, "KSTAT<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
            break;
        case F1_DSK_LOAD_KCOMM:
            ret = snprintf(buffer, buffer_size, "KCOMM<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
            break;
        case F1_DSK_LOAD_KADR:
            ret = snprintf(buffer, buffer_size, "KADR<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
            break;
        case F1_DSK_LOAD_KDATA:
            ret = snprintf(buffer, buffer_size, "KDATA<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
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
            ret = snprintf(buffer, buffer_size, "MD<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
        }
        break;
    default:
        switch (task) {
        case TASK_EMULATOR:
            switch (f2) {
            case F2_EMU_LOAD_DNS:
                ret = snprintf(buffer, buffer_size, "DNS<- ");
                n_buffer += ret;
                UPDATE_BUFFER(buffer, buffer_size, ret)
                break;
            case F2_EMU_LOAD_IR:
                ret = snprintf(buffer, buffer_size, "IR<- ");
                n_buffer += ret;
                UPDATE_BUFFER(buffer, buffer_size, ret)
                break;
            default:
                break;
            }
            break;
        case TASK_ETHERNET:
            switch (f2) {
            case F2_ETH_EODFCT:
                ret = snprintf(buffer, buffer_size, "EODFCT<- ");
                n_buffer += ret;
                UPDATE_BUFFER(buffer, buffer_size, ret)
                break;
            default:
                break;
            }
            break;
        case TASK_DISPLAY_WORD:
            switch (f2) {
            case F2_DW_LOAD_DDR:
                ret = snprintf(buffer, buffer_size, "DDR<- ");
                n_buffer += ret;
                UPDATE_BUFFER(buffer, buffer_size, ret)
                break;
            default:
                break;
            }
            break;
        case TASK_CURSOR:
            switch (f2) {
            case F2_CUR_LOAD_XPREG:
                ret = snprintf(buffer, buffer_size, "XPREG<- ");
                n_buffer += ret;
                UPDATE_BUFFER(buffer, buffer_size, ret)
                break;
            case F2_CUR_LOAD_CSR:
                ret = snprintf(buffer, buffer_size, "CSR<- ");
                n_buffer += ret;
                UPDATE_BUFFER(buffer, buffer_size, ret)
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    return n_buffer;
}

/* Disassembles assigments from the bus.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_assign_bus(struct disassembler *dis,
                      uint32_t microcode, uint8_t task,
                      char *buffer, size_t buffer_size)
{
    int ret, n_buffer;

    n_buffer = 0;
    ret = disasm_bus_lhs(dis, microcode, task, FALSE,
                         buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    if (n_buffer == 0) return 0;

    ret = disasm_bus_rhs(dis, microcode, task,
                         buffer, buffer_size);
    n_buffer += ret;
    return n_buffer;
}

/* Disassembles the alu source.
 * Returns the number of character written in the buffer.
 */
static
int disasm_alu_rhs(struct disassembler *dis,
                   uint32_t microcode, uint8_t task,
                   char *buffer, size_t buffer_size)
{
    uint16_t aluf;
    int n_buffer, ret;

    aluf = MICROCODE_ALUF(microcode);

    n_buffer = 0;
    if (aluf != ALU_T) {
        ret = disasm_bus_rhs(dis, microcode, task,
                             buffer, buffer_size);
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    switch (aluf) {
    case ALU_BUS:
        break;
    case ALU_T:
        ret = snprintf(buffer, buffer_size, "T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_OR_T:
        ret = snprintf(buffer, buffer_size, " OR T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_AND_T:
        ret = snprintf(buffer, buffer_size, " AND T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_XOR_T:
        ret = snprintf(buffer, buffer_size, " XOR T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_PLUS_1:
        ret = snprintf(buffer, buffer_size, " + 1");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_MINUS_1:
        ret = snprintf(buffer, buffer_size, " - 1");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_PLUS_T:
        ret = snprintf(buffer, buffer_size, " + T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_MINUS_T:
        ret = snprintf(buffer, buffer_size, " - T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_MINUS_T_MINUS_1:
        ret = snprintf(buffer, buffer_size, " - T - 1");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_PLUS_T_PLUS_1:
        ret = snprintf(buffer, buffer_size, " + T + 1");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_PLUS_SKIP:
        ret = snprintf(buffer, buffer_size, " + SKIP");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_AND_T_WB:
        ret = snprintf(buffer, buffer_size, " . T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    case ALU_BUS_AND_NOT_T:
        ret = snprintf(buffer, buffer_size, " AND NOT T");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
        break;
    }

    return n_buffer;
}

/* Disassembles the alu destinations.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_alu_lhs(struct disassembler *dis,
                   uint32_t microcode, uint8_t task,
                   char *buffer, size_t buffer_size)
{
    uint16_t f1, f2;
    uint16_t aluf;
    int load_t, load_l;
    int load_t_from_alu;
    int ret, n_buffer;

    aluf = MICROCODE_ALUF(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);
    load_t = MICROCODE_T(microcode);
    load_l = MICROCODE_L(microcode);

    load_t_from_alu = LOAD_T_FROM_ALU(aluf);

    n_buffer = 0;
    ret = disasm_bus_lhs(dis, microcode, task, TRUE,
                         buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    if (load_t && (load_t_from_alu)) {
        ret = snprintf(buffer, buffer_size, "T<- ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    if (load_l) {
        ret = snprintf(buffer, buffer_size, "L<- ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    switch (f1) {
    case F1_LOAD_MAR:
        if (f2 == F2_STORE_MD) {
            ret = snprintf(buffer, buffer_size, "XMAR<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
        } else {
            ret = snprintf(buffer, buffer_size, "MAR<- ");
            n_buffer += ret;
            UPDATE_BUFFER(buffer, buffer_size, ret)
        }
        break;
    default:
        break;
    }

    return n_buffer;
}

/* Disassembles assigments from the alu.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_assign_alu(struct disassembler *dis,
                      uint32_t microcode, uint8_t task,
                      char *buffer, size_t buffer_size)
{
    int ret, n_buffer;

    n_buffer = 0;
    ret = disasm_alu_lhs(dis, microcode, task,
                         buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    if (n_buffer == 0) return 0;

    ret = disasm_alu_rhs(dis, microcode, task,
                         buffer, buffer_size);
    n_buffer += ret;
    return n_buffer;
}

/* Disassembles the L register source.
 * Returns the number of character written in the buffer.
 */
static
int disasm_lreg_rhs(struct disassembler *dis,
                    uint32_t microcode, uint8_t task,
                    char *buffer, size_t buffer_size)
{
    uint16_t f1, f2;

    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    switch (f1) {
    case F1_LLSH1:
        if (f2 == F2_EMU_MAGIC) {
            return snprintf(buffer, buffer_size, "L MLSH 1");
        } else {
            return snprintf(buffer, buffer_size, "L LSH 1");
        }
    case F1_LRSH1:
        if (f2 == F2_EMU_MAGIC) {
            return snprintf(buffer, buffer_size, "L MRSH 1");
        } else {
            return snprintf(buffer, buffer_size, "L RSH 1");
        }
    case F1_LLCY8:
        return snprintf(buffer, buffer_size, "L LCY 8");
    default:
        return snprintf(buffer, buffer_size, "L");
    }
}

/* Disassembles the L register destinations.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_lreg_lhs(struct disassembler *dis,
                    uint32_t microcode, uint8_t task,
                    char *buffer, size_t buffer_size)
{
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;
    int use_constant;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    use_constant = (f1 == F1_CONSTANT || f2 == F2_CONSTANT
                    || BS_USE_CROM(bs));

    if (bs == BS_LOAD_R && (!use_constant)) {
        return snprintf(buffer, buffer_size,
                        "R%o<- ", rsel);
    }

    return 0;
}

/* Disassembles assigments from the L register.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_assign_lreg(struct disassembler *dis,
                       uint32_t microcode, uint8_t task,
                       char *buffer, size_t buffer_size)
{
    int ret, n_buffer;

    n_buffer = 0;
    ret = disasm_lreg_lhs(dis, microcode, task,
                          buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    if (n_buffer == 0) return 0;

    ret = disasm_lreg_rhs(dis, microcode, task,
                          buffer, buffer_size);
    n_buffer += ret;
    return n_buffer;
}

/* Disassembles the SINK (bus) register destinations.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_sink_bus_lhs(struct disassembler *dis,
                        uint32_t microcode, uint8_t task,
                        char *buffer, size_t buffer_size)
{
    uint16_t rsel;
    uint16_t aluf;
    uint16_t bs;
    uint16_t f1, f2;
    char tbuf[1];
    int ret;

    rsel = MICROCODE_RSEL(microcode);
    aluf = MICROCODE_ALUF(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    ret = disasm_bus_lhs(dis, microcode, task, FALSE,
                         tbuf, sizeof(tbuf));
    if (ret != 0) return 0;

    if (aluf != ALU_T) {
        ret = disasm_alu_lhs(dis, microcode, task,
                             tbuf, sizeof(tbuf));
        if (ret != 0) return 0;
    }

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT)
        goto do_sink;

    switch (bs) {
    case BS_READ_R:
        if (rsel != 0) break;
        if (task != TASK_EMULATOR)
            return 0;
        if (f2 != F2_EMU_ACDEST && f2 != F2_EMU_ACSOURCE)
            return 0;
        break;
    case BS_LOAD_R:
        return 0;
    case BS_NONE:
        if (task == TASK_EMULATOR && f1 == F1_EMU_RSNF)
            break;
        else if (task == TASK_ETHERNET && f1 == F1_ETH_EILFCT)
            break;
        else if (task == TASK_ETHERNET && f1 == F1_ETH_EPFCT)
            break;
        return 0;
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
        return 0;
    }

do_sink:
    return snprintf(buffer, buffer_size, "SINK<- ");
}

/* Disassembles assigments to the SINK register (for bus).
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_assign_sink_bus(struct disassembler *dis,
                           uint32_t microcode, uint8_t task,
                           char *buffer, size_t buffer_size)
{
    int ret, n_buffer;

    n_buffer = 0;
    ret = disasm_sink_bus_lhs(dis, microcode, task,
                              buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    if (n_buffer == 0) return 0;

    ret = disasm_bus_rhs(dis, microcode, task,
                         buffer, buffer_size);
    n_buffer += ret;
    return n_buffer;
}

/* Disassembles the SINK source (for constants).
 * Returns the number of character written in the buffer.
 */
static
int disasm_sink_const_rhs(struct disassembler *dis,
                          uint32_t microcode, uint8_t task,
                          char *buffer, size_t buffer_size)
{
    uint16_t rsel;

    rsel = MICROCODE_RSEL(microcode);
    return snprintf(buffer, buffer_size,
                    "R%o", rsel);
}

/* Disassembles the SINK (constant) register destinations.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_sink_const_lhs(struct disassembler *dis,
                          uint32_t microcode, uint8_t task,
                          char *buffer, size_t buffer_size)
{
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT)
        return 0;

    if (!BS_USE_CROM(bs)) return 0;

    if (rsel == 0) return 0;

    return snprintf(buffer, buffer_size, "SINK<- ");
}

/* Disassembles assigments to the SINK register (for constants).
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_assign_sink_const(struct disassembler *dis,
                             uint32_t microcode, uint8_t task,
                             char *buffer, size_t buffer_size)
{
    int ret, n_buffer;

    n_buffer = 0;
    ret = disasm_sink_const_lhs(dis, microcode, task,
                                buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    if (n_buffer == 0) return 0;

    ret = disasm_sink_const_rhs(dis, microcode, task,
                                buffer, buffer_size);
    n_buffer += ret;
    return n_buffer;
}


/* Disassembles the GOTO part of the instruction.
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_goto(struct disassembler *dis,
                uint32_t microcode, uint8_t task,
                char *buffer, size_t buffer_size)
{
    uint16_t next;
    next = MICROCODE_NEXT(microcode);

    return snprintf(buffer, buffer_size,
                    ":0%04o", next);
}

/* Disassembles some extra comments (helpful for debugging).
 * This is an auxiliary function used by disassembler_disassemble().
 * Returns the number of character written in the buffer.
 */
static
int disasm_comments(struct disassembler *dis,
                    uint32_t microcode, uint8_t task,
                    char *buffer, size_t buffer_size)
{
    uint16_t rsel;
    uint16_t bs;
    uint16_t f1, f2;
    uint16_t c, caddr;

    rsel = MICROCODE_RSEL(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);

    if (f1 != F1_CONSTANT && f2 != F2_CONSTANT && (!BS_USE_CROM(bs)))
        return 0;

    caddr = CONST_ADDR(rsel, bs);
    c = dis->consts[caddr];

    return snprintf(buffer, buffer_size,
                    "; C(%03o) = %o",
                    caddr, c);
}

int disassembler_disassemble(struct disassembler *dis,
                             uint16_t address, uint8_t task,
                             char *buffer, size_t buffer_size)
{
    uint32_t microcode;
    uint16_t rsel;
    uint16_t aluf;
    uint16_t bs;
    uint16_t f1, f2;
    int load_t, load_l;
    uint16_t next;
    int n_buffer, ret;
    int has_something;

    if (unlikely(address >= MICROCODE_SIZE)) return -1;
    microcode = dis->microcode[address];

    rsel = MICROCODE_RSEL(microcode);
    aluf = MICROCODE_ALUF(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);
    load_t = MICROCODE_T(microcode);
    load_l = MICROCODE_L(microcode);
    next = MICROCODE_NEXT(microcode);

    ret = snprintf(buffer, buffer_size,
                   "%05o   %02o    %011o  %02o   %02o   %o  "
                   "%02o %02o %o %o %04o   ",
                   address, task, microcode, rsel, aluf, bs,
                   f1, f2, load_t, load_l, next);
    n_buffer = ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)


    ret = disasm_nondata_function(dis, microcode, task,
                                  buffer, buffer_size);
    has_something = (ret > 0);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    /* Print a comma. */
    if (has_something) {
        ret = snprintf(buffer, buffer_size, ", ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    ret = disasm_assign_bus(dis, microcode, task,
                            buffer, buffer_size);
    has_something = (ret > 0);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    /* Print a comma. */
    if (has_something) {
        ret = snprintf(buffer, buffer_size, ", ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    ret = disasm_assign_alu(dis, microcode, task,
                            buffer, buffer_size);
    has_something = (ret > 0);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    /* Print a comma. */
    if (has_something) {
        ret = snprintf(buffer, buffer_size, ", ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    ret = disasm_assign_lreg(dis, microcode, task,
                             buffer, buffer_size);
    has_something = (ret > 0);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    /* Print a comma. */
    if (has_something) {
        ret = snprintf(buffer, buffer_size, ", ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    ret = disasm_assign_sink_bus(dis, microcode, task,
                                 buffer, buffer_size);
    has_something = (ret > 0);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    /* Print a comma. */
    if (has_something) {
        ret = snprintf(buffer, buffer_size, ", ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    ret = disasm_assign_sink_const(dis, microcode, task,
                                   buffer, buffer_size);
    has_something = (ret > 0);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    /* Print a comma. */
    if (has_something) {
        ret = snprintf(buffer, buffer_size, ", ");
        n_buffer += ret;
        UPDATE_BUFFER(buffer, buffer_size, ret)
    }

    ret = disasm_goto(dis, microcode, task,
                      buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    ret = disasm_comments(dis, microcode, task,
                          buffer, buffer_size);
    n_buffer += ret;
    UPDATE_BUFFER(buffer, buffer_size, ret)

    return n_buffer;
}

