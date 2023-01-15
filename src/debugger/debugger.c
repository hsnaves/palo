
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "debugger/debugger.h"
#include "simulator/simulator.h"
#include "gui/gui.h"
#include "assembler/objfile.h"
#include "common/allocator.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Constants. */
#define MAX_BREAKPOINTS                 1024
#define BUFFER_SIZE                     8192

/* Functions. */

void debugger_initvar(struct debugger *dbg)
{
    allocator_initvar(&dbg->salloc);
    allocator_initvar(&dbg->oalloc);
    objfile_initvar(&dbg->rom0f);

    dbg->bps = NULL;
    dbg->cmd_buf = NULL;

    string_buffer_initvar(&dbg->output);
}

void debugger_destroy(struct debugger *dbg)
{
    allocator_destroy(&dbg->salloc);
    allocator_destroy(&dbg->oalloc);
    objfile_destroy(&dbg->rom0f);

    if (dbg->bps) free((void *) dbg->bps);
    dbg->bps = NULL;

    if (dbg->cmd_buf) free((void *) dbg->cmd_buf);
    dbg->cmd_buf = NULL;

    string_buffer_destroy(&dbg->output);
}

int debugger_create(struct debugger *dbg, int use_debugger,
                    struct simulator *sim, struct gui *ui)
{
    debugger_initvar(dbg);

    if (unlikely(!allocator_create(&dbg->salloc, 0))) {
        report_error("debugger: create: "
                     "could not create string allocator");
        debugger_destroy(dbg);
        return FALSE;
    }

    if (unlikely(!allocator_create(&dbg->oalloc, DEFAULT_ALIGNMENT))) {
        report_error("debugger: create: "
                     "could not create object allocator");
        debugger_destroy(dbg);
        return FALSE;
    }

    if (unlikely(!objfile_create(&dbg->rom0f,
                                 &dbg->salloc, &dbg->oalloc))) {
        report_error("debugger: create: could not create "
                     "ROM0 objfile");
        debugger_destroy(dbg);
        return FALSE;
    }

    dbg->max_breakpoints = MAX_BREAKPOINTS;
    dbg->cmd_buf_size = BUFFER_SIZE;

    dbg->bps = (struct breakpoint *)
        malloc(dbg->max_breakpoints * sizeof(struct breakpoint));
    dbg->cmd_buf = (char *) malloc(dbg->cmd_buf_size);

    if (unlikely(!dbg->bps || !dbg->cmd_buf)) {
        report_error("debugger: create: memory exhausted");
        debugger_destroy(dbg);
        return FALSE;
    }

    if (unlikely(!string_buffer_create(&dbg->output, BUFFER_SIZE))) {
        report_error("debugger: create: could not create string_buffer");
        debugger_destroy(dbg);
        return FALSE;
    }

    dbg->frequency = 6000000; /* 6 MHz */
    dbg->use_octal = TRUE;
    dbg->use_debugger = use_debugger;
    dbg->sim = sim;
    dbg->ui = ui;

    debugger_clear(dbg);

    return TRUE;
}

void debugger_clear(struct debugger *dbg)
{
    size_t num;

    allocator_clear(&dbg->salloc);
    allocator_clear(&dbg->oalloc);
    objfile_clear(&dbg->rom0f);

    for (num = 1; num < dbg->max_breakpoints; num++) {
        dbg->bps[num].available = TRUE;
    }

    string_buffer_clear(&dbg->output);
}

int debugger_load_binary(struct debugger *dbg,
                         const char *filename, uint8_t bank)
{
    if (unlikely(bank >= 1)) {
        report_error("debugger: load_binary: "
                     "invalid bank `%u`", bank);
        return FALSE;
    }

    if (unlikely(!objfile_load_binary(&dbg->rom0f, filename))) {
        report_error("debugger: load_binary: "
                     "could not load binary");
        return FALSE;
    }

    return TRUE;
}

/* Auxiliary function used by debugger_nova_disassemble().
 * Callback to print constants, registers, labels, etc.
 */
static
void disasm_decode_cb(struct value_decoder *vdec,
                      enum decode_type dec_type, uint32_t val)
{
    struct decoder *dec;
    const struct debugger *dbg;
    const struct simulator *sim;
    struct string_buffer *output;

    dec = vdec->dec;
    dbg = (const struct debugger *) vdec->arg;
    sim = dbg->sim;
    output = dec->output;

    switch (dec_type) {
    case DECODE_CONST:
        if (val >= CONSTANT_SIZE) {
            dec->error = TRUE;
            return;
        }
        if (dbg->use_octal) {
            string_buffer_print(output, "%o", sim->consts[val]);
        } else {
            string_buffer_print(output, "0x%X", sim->consts[val]);
        }
        break;

    case DECODE_REG:
        if (val >= NUM_R_REGISTERS + NUM_S_REGISTERS) {
            dec->error = TRUE;
            return;
        }

        /* Always use octal for register numbers. */
        if (val < NUM_R_REGISTERS) {
            string_buffer_print(output, "R%o", val);
        } else if (val == NUM_R_REGISTERS) {
            string_buffer_print(output, "M");
        } else {
            string_buffer_print(output, "S%o", val & R_MASK);
        }
        break;

    case DECODE_LABEL:
        if (dbg->use_octal) {
            string_buffer_print(output, "%05o", (uint16_t) val);
        } else {
            string_buffer_print(output, "0x%04X", (uint16_t) val);
        }
        break;

    case DECODE_MEMORY:
        if (dbg->use_octal) {
            string_buffer_print(output, "%07o", (uint16_t) val);
        } else {
            string_buffer_print(output, "0x%04X", (uint16_t) val);
        }
        break;

    case DECODE_TASK:
        if (val >= TASK_NUM_TASKS) {
            dec->error = TRUE;
            return;
        }

        string_buffer_print(output, "%s", TASK_NAMES[val]);
        break;

    case DECODE_BOOL:
        string_buffer_print(output, "%d", (val) ? 1 : 0);
        break;

    case DECODE_VALUE:
        if (dbg->use_octal) {
            string_buffer_print(output, "%07o", (uint16_t) val);
        } else {
            string_buffer_print(output, "0x%04X", (uint16_t) val);
        }
        break;

    case DECODE_VALUE32:
        if (dbg->use_octal) {
            string_buffer_print(output, "%012o", val);
        } else {
            string_buffer_print(output, "0x%08X", val);
        }
        break;

    case DECODE_SVALUE32:
        string_buffer_print(output, "%d", (int32_t) val);
        break;
    }
}

void debugger_setup_value_decoder(struct debugger *dbg,
                                  struct value_decoder *vdec)
{
    vdec->arg = (void *) dbg;
    vdec->cb = &disasm_decode_cb;
}

struct decoder *debugger_setup_decoder(struct debugger *dbg)
{
    const struct simulator *sim;
    struct decoder *dec;

    sim = dbg->sim;
    simulator_predecode(sim, &dbg->mc);

    dec = &dbg->dec;
    dec->error = FALSE;
    dec->output = &dbg->output;
    dec->mc = &dbg->mc;
    dec->vdec = &dbg->vdecs[0];

    dbg->vdecs[0].dec = dec;
    dbg->vdecs[0].next = &dbg->vdecs[1];

    dbg->vdecs[1].dec = dec;
    dbg->vdecs[1].next = NULL;

    objfile_setup_value_decoder(&dbg->rom0f, &dbg->vdecs[0]);
    debugger_setup_value_decoder(dbg, &dbg->vdecs[1]);

    string_buffer_clear(dec->output);
    return dec;
}

void debugger_disassemble(struct debugger *dbg)
{
    const struct microcode *mc;
    struct decoder *dec;
    struct string_buffer *output;
    size_t len;

    dec = debugger_setup_decoder(dbg);
    output = dec->output;
    mc = dec->mc;

    len = string_buffer_length(output);
    decode_value(dec->vdec, DECODE_TASK, mc->task);
    string_buffer_print(output, "-");
    decode_value(dec->vdec, DECODE_LABEL, mc->address);
    while (string_buffer_length(output) < len + 14)
        string_buffer_print(output, " ");

    string_buffer_print(output, " ");
    decode_value(dec->vdec, DECODE_VALUE32, mc->mcode);
    string_buffer_print(output, "   ");
    decode_microcode(dec);

    if (mc->use_constant || mc->bs_use_crom) {
        string_buffer_print(output, "; ");
        decode_value(dec->vdec, DECODE_CONST,mc->const_addr);
        string_buffer_print(output, " = ");
        decode_value(dec->vdec, DECODE_VALUE,
                     dbg->sim->consts[mc->const_addr]);
    }
}

void debugger_nova_disassemble(struct debugger *dbg)
{
    const struct simulator *sim;
    struct decoder *dec;
    struct string_buffer *output;
    struct nova_insn ni;

    dec = debugger_setup_decoder(dbg);
    output = dec->output;

    sim = dbg->sim;
    simulator_nova_predecode(sim, &ni);

    decode_value_padded(dec->vdec, DECODE_MEMORY, ni.address, 12);
    decode_value(dec->vdec, DECODE_VALUE, ni.insn);
    string_buffer_print(output, " --- ");
    nova_insn_decode(dec, &ni);
}
