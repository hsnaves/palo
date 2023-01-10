
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "debugger/debugger.h"
#include "simulator/simulator.h"
#include "gui/gui.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Constants. */
#define MAX_BREAKPOINTS                 1024
#define BUFFER_SIZE                     8192

/* Functions. */

void debugger_initvar(struct debugger *dbg)
{
    dbg->bps = NULL;
    dbg->cmd_buf = NULL;
    string_buffer_initvar(&dbg->output);
}

void debugger_destroy(struct debugger *dbg)
{
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

    dbg->use_debugger = use_debugger;
    dbg->sim = sim;
    dbg->ui = ui;

    return TRUE;
}

/* Auxiliary function used by debugger_disassemble().
 * Callback to print constants, registers, labels, etc.
 */
static
void disasm_decode_cb(struct decoder *dec,
                      enum decode_type dec_type, uint32_t val,
                      void *arg)
{
    const struct debugger *dbg;
    const struct simulator *sim;
    struct string_buffer *output;

    dbg = (const struct debugger *) arg;
    sim = dbg->sim;
    output = dec->output;

    switch (dec_type) {
    case DECODE_CONST:
        if (val < CONSTANT_SIZE) {
            string_buffer_print(output, "%o", sim->consts[val]);
        } else {
            dec->error = TRUE;
        }
        break;
    case DECODE_REG:
        if (val <= R_MASK) {
            string_buffer_print(output, "R%o", val);
        } else if (val <= 2 * R_MASK + 1) {
            string_buffer_print(output, "S%o", val & R_MASK);
        } else {
            dec->error = TRUE;
        }
        break;
    case DECODE_LABEL:
        string_buffer_print(output, "%05o", (uint16_t) val);
        break;
    case DECODE_MEMORY:
        string_buffer_print(output, "%07o", (uint16_t) val);
        break;
    case DECODE_VALUE:
        string_buffer_print(output, "%07o", (uint16_t) val);
        break;
    case DECODE_VALUE32:
        string_buffer_print(output, "%012o", val);
        break;
    case DECODE_SVALUE32:
        string_buffer_print(output, "%012o", (int32_t) val);
        break;
    }
}

void debugger_disassemble(struct debugger *dbg)
{
    const struct simulator *sim;
    struct decoder dec;
    struct microcode mc;

    sim = dbg->sim;

    dec.arg = (void *) dbg;
    dec.dec_cb = &disasm_decode_cb;
    dec.output = &dbg->output;
    dec.error = FALSE;

    simulator_predecode(sim, &mc);
    string_buffer_print(&dbg->output,
                        "%03o-%07o %012o --- ",
                        mc.task, mc.address, mc.mcode);

    microcode_decode(&dec, &mc);
}

void debugger_nova_disassemble(struct debugger *dbg)
{
    const struct simulator *sim;
    struct decoder dec;
    struct nova_insn ni;

    sim = dbg->sim;

    dec.arg = (void *) dbg;
    dec.dec_cb = &disasm_decode_cb;
    dec.output = &dbg->output;
    dec.error = FALSE;

    simulator_nova_predecode(sim, &ni);

    string_buffer_print(&dbg->output,
                        "%07o %07o --- ",
                        ni.address, ni.insn);

    nova_insn_decode(&dec, &ni);
}
