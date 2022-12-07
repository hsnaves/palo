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

    for (i = 0; i < MICROCODE_SIZE; i++) {
        printf("0x%04X: 0x%X\n", i, dis->task_mask[i]);
    }
}
