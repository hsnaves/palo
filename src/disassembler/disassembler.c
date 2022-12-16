#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "disassembler/disassembler.h"
#include "microcode/microcode.h"
#include "common/allocator.h"
#include "common/utils.h"

/* Constants. */
#define INSN_VISITED              1
#define INSN_PENDING              2
#define INSN_NOT_VALID            4

/* Functions. */

void disassembler_initvar(struct disassembler *dis)
{
    allocator_initvar(&dis->oalloc);

    dis->consts = NULL;
    dis->microcode = NULL;
    dis->insns  = NULL;
    dis->stack = NULL;
}

void disassembler_destroy(struct disassembler *dis)
{
    if (dis->consts) free((void *) dis->consts);
    dis->consts = NULL;

    if (dis->microcode) free((void *) dis->microcode);
    dis->microcode = NULL;

    if (dis->insns) free((void *) dis->insns);
    dis->insns = NULL;

    if (dis->stack) free((void *) dis->stack);
    dis->stack = NULL;

    allocator_destroy(&dis->oalloc);
}

int disassembler_create(struct disassembler *dis)
{
    disassembler_initvar(dis);

    if (unlikely(!allocator_create(&dis->oalloc, DEFAULT_ALIGNMENT))) {
        report_error("disassembler: create: could not create allocator");
        disassembler_destroy(dis);
        return FALSE;
    }

    dis->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    dis->microcode = (uint32_t *)
        malloc(MICROCODE_SIZE * sizeof(uint32_t));
    dis->insns = (struct instruction *)
        malloc(MICROCODE_SIZE * sizeof(struct instruction));
    dis->stack = (uint16_t *)
        malloc(MICROCODE_SIZE * sizeof(uint16_t));

    if (unlikely(!dis->consts || !dis->microcode
                 || !dis->insns || !dis->stack)) {
        report_error("disassembler: create: memory exhausted");
        disassembler_destroy(dis);
        return FALSE;
    }

    dis->free_nodes = NULL;
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

/* Allocates an address_node.
 * Returns the allocated node.
 */
static
struct address_node *alloc_node(struct disassembler *dis)
{
    struct address_node *n;
    size_t size;

    if (!dis->free_nodes) {
        size = sizeof(struct address_node);
        dis->free_nodes = allocator_alloc(&dis->oalloc, size, FALSE);
        if (unlikely(!dis->free_nodes)) {
            report_error("disassembler: find_task_addresses: "
                         "memory exhausted");
            return NULL;
        }
        dis->free_nodes->next = NULL;
    }

    n = dis->free_nodes;
    dis->free_nodes = n->next;
    n->next = NULL;
    return n;
}

/* Clears all the information about the instructions
 * (except task_mask).
 */
static
void clear_insns(struct disassembler *dis)
{
    struct instruction *insn;
    struct address_node *n;
    uint16_t address;

    for (address = 0; address < MICROCODE_SIZE; address++) {
        insn = &dis->insns[address];
        insn->details = 0;

        n = insn->callers;
        while (n) {
            insn->callers = n->next;
            n->next = dis->free_nodes;
            dis->free_nodes = n;
            n = insn->callers;
        }
    }
}

/* Adds a new "call" (or jump) from an address to another.
 * The jump is from the address in the parameter `from` to the address in
 * the parameter `to`. The next mask used for this jump is `next_mask`.
 * The following next mask is given by `following_next_mask`, and
 * the current task is `task`.
 * Returns FALSE on an error.
 */
static
int add_call(struct disassembler *dis, uint16_t from, uint16_t to,
             uint16_t next_mask, uint32_t following_next_mask, uint8_t task)
{
    struct address_node *n;
    struct instruction *insn;
    int modified, found;

    insn = &dis->insns[to];

    modified = (!(insn->details & (INSN_VISITED | INSN_PENDING)));

    found = FALSE;
    for (n = insn->callers; n; n = n->next) {
        if (n->following_next_mask == following_next_mask) {
            found = TRUE;
        }
    }
    modified = modified || (!found);
    if (!modified) return TRUE;

    n = alloc_node(dis);
    if (unlikely(!n)) return FALSE;

    n->address = from;
    n->next_mask = next_mask;
    n->following_next_mask = following_next_mask;

    n->next = insn->callers;
    insn->callers = n;

    if (!(insn->details & INSN_PENDING)) {
        /* Add it to the stack. */
        dis->stack[dis->stack_top++] = to;
        insn->details |= INSN_PENDING;
    }

    return TRUE;
}

/* Propagates the known information about instruction of a given task.
 * This function will try to figure out which addresses correspond to
 * which tasks.
 * The current task being analyzed is given by `task`.
 * Returns TRUE on success.
 */
static
int propagate_information(struct disassembler *dis, uint8_t task)
{
    struct instruction *insn;
    struct address_node *n;
    uint16_t address, next, t, bm;
    uint16_t rsel, bs;
    uint32_t microcode;
    uint32_t next_mask, following_next_mask;
    uint32_t prev_next_mask;
    int should_continue;

    while (dis->stack_top > 0) {
        /* Pop an element from the stack. */
        address = dis->stack[--(dis->stack_top)];
        insn = &dis->insns[address];

        insn->details &= ~INSN_PENDING;
        insn->details |= INSN_VISITED;

        microcode = dis->microcode[address];

        rsel = MICROCODE_RSEL(microcode);
        bs = MICROCODE_BS(microcode);
        next = MICROCODE_NEXT(microcode);

        t = microcode_guess_tasks(microcode);
        if (!(t & (1 << task))) {
            /* This microcode cannot run in this task. */
            insn->details |= INSN_NOT_VALID;
        }

        following_next_mask = microcode_next_mask(microcode, task);
        if (following_next_mask & NEXT_MASK_CONSTANT) {
            bm = dis->consts[CONST_ADDR(rsel, bs)];
            bm &= (uint16_t) following_next_mask;
            following_next_mask &= ~(0xFFFF);
            following_next_mask |= bm;
        }

        if (insn->details & INSN_NOT_VALID) {
            /* TODO: Backpropagate this information. */
        } else {
            should_continue = TRUE;
            for (next_mask = 0; should_continue; next_mask++) {
                should_continue = FALSE;
                for (n = insn->callers; n; n = n->next) {
                    prev_next_mask = n->following_next_mask;
                    bm = prev_next_mask & 0xFFFF;

                    if (prev_next_mask & NEXT_MASK_BUS) {
                        /* Skip BUS jumps for now. */
                        if (bm == 0xFFFF) {
                            if (next == 0) continue;
                            bm = 0x0000;
                        }
                    }

                    if (next_mask < bm) should_continue = TRUE;
                    if ((bm | next_mask) != bm) continue;

                    if (prev_next_mask & NEXT_MASK_DSK_INIT) {
                        if (next_mask != 0 && next_mask != 0x1F)
                            continue;
                    }

                    if (unlikely(!add_call(dis, address, next | next_mask,
                                           next_mask, following_next_mask,
                                           task)))
                        return FALSE;
                }
            }
        }
    }
    return TRUE;
}

int disassembler_find_task_addresses(struct disassembler *dis)
{
    uint16_t address, task, task_mask;
    struct instruction *insn;

    memset(dis->insns, 0, MICROCODE_SIZE * sizeof(struct instruction));

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

        clear_insns(dis);
        dis->stack_top = 0;

        address = task;
        if (unlikely(!add_call(dis, address, address, 0, 0, task)))
            return FALSE;
        if (unlikely(!propagate_information(dis, task)))
            return FALSE;
        for (address = 0; address < MICROCODE_SIZE; address++) {
            insn = &dis->insns[address];
            if (insn->details & INSN_VISITED) {
                insn->task_mask |= (1 << task);
            }
        }
    }

    return TRUE;
}

/* Auxiliary function used by disassembler_disassemble().
 * Callback to print constants.
 */
static
void disasm_constant_cb(const struct decoder *dec, uint16_t val,
                        struct decode_buffer *output)
{
    struct disassembler *dis;
    dis = (struct disassembler *) dec->arg;
    decode_buffer_print(output, "%o", dis->consts[val]);
}

/* Auxiliary function used by disassembler_disassemble().
 * Callback to print R registers.
 */
static
void disasm_register_cb(const struct decoder *dec, uint16_t val,
                        struct decode_buffer *output)
{
    if (val <= R_MASK) {
        decode_buffer_print(output, "R%o", val);
    } else {
        decode_buffer_print(output, "S%o", val & R_MASK);
    }
}

/* Auxiliary function used by disassembler_disassemble().
 * Callback to print GOTO statements.
 */
static
void disasm_goto_cb(const struct decoder *dec, uint16_t val,
                    struct decode_buffer *output)
{
    decode_buffer_print(output, ":%05o", val);
}

int disassembler_disassemble(struct disassembler *dis,
                             uint16_t address, uint8_t task,
                             char *output, size_t output_size)
{
    struct decoder dec;
    struct decode_buffer out;
    uint32_t microcode;
    uint16_t rsel;
    uint16_t aluf;
    uint16_t bs;
    uint16_t f1, f2;
    int load_t, load_l;
    uint16_t next;

    if (unlikely(address >= MICROCODE_SIZE)) return FALSE;

    out.buf = output;
    out.buf_size = output_size;
    decode_buffer_reset(&out);

    microcode = dis->microcode[address];
    rsel = MICROCODE_RSEL(microcode);
    aluf = MICROCODE_ALUF(microcode);
    bs = MICROCODE_BS(microcode);
    f1 = MICROCODE_F1(microcode);
    f2 = MICROCODE_F2(microcode);
    load_t = MICROCODE_T(microcode);
    load_l = MICROCODE_L(microcode);
    next = MICROCODE_NEXT(microcode);

    decode_buffer_print(&out,
                        "%05o   %02o    %011o  %02o   %02o   %o  "
                        "%02o %02o %o %o %04o   ",
                        address, task, microcode, rsel, aluf, bs,
                        f1, f2, load_t, load_l, next);

    dec.address = address;
    dec.microcode = microcode;
    dec.task = task;
    dec.arg = dis;
    dec.const_cb = &disasm_constant_cb;
    dec.reg_cb = &disasm_register_cb;
    dec.goto_cb = &disasm_goto_cb;

    decoder_decode(&dec, &out);
    return TRUE;
}

