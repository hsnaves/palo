
#ifndef __DEBUGGER_DEBUGGER_H
#define __DEBUGGER_DEBUGGER_H

#include <stddef.h>
#include <stdint.h>
#include "simulator/simulator.h"
#include "gui/gui.h"
#include "common/utils.h"

/* Data structures and types. */

/* Structure defining a breakpoint. */
struct breakpoint {
    int available;                /* Breakpoint available. */
    int enable;                   /* Breakpoint enabled. */
    uint8_t task;                 /* The task of the breakpoint. */
    uint8_t ntask;                /* The next task of the breakpoint. */
    uint16_t mpc;                 /* The micro program counter. */
    int on_task_switch;           /* Only enable on task switch. */
    uint32_t mir_fmt;             /* The format of the micro instruction
                                   * to define a break point.
                                   */
    uint32_t mir_mask;            /* The mask for the micro instruction
                                   * breakpoint.
                                   */
    int allow_constants;          /* To allow F1 or F2 constants in MIR. */
    uint16_t rsel;                /* Which R register to watch. */
    int r_watch;                  /* To watch for R modification. */
    uint16_t addr;                /* Address watch. */
    int watch;                    /* To watch an address. */
};

/* Internal structure for the palos simulator. */
struct debugger {
    struct simulator *sim;        /* The simulator. */
    struct gui *ui;               /* The user interface. */

    size_t max_breakpoints;       /* The maximum number of breakpoints. */
    struct breakpoint *bps;       /* The breakpoints. */

    char *cmd_buf;                /* Buffer for command. */
    size_t cmd_buf_size;          /* Size of the command buffer. */

    char *out_buf;                /* Buffer for output. */
    size_t out_buf_size;          /* Size of the output buffer. */
    struct string_buffer output;  /* The string buffer for output. */
};

/* Functions. */

/* Initializes the debugger variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void debugger_initvar(struct debugger *dgb);

/* Destroys the debugger object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void debugger_destroy(struct debugger *dbg);

/* Creates a new debugger object.
 * This obeys the initvar / destroy / create protocol.
 * The parameters `sim` and `ui` have references to a simulator
 * object and a gui object.
 * Returns TRUE on success.
 */
int debugger_create(struct debugger *ps,
                    struct simulator *sim,
                    struct gui *ui);

/* To run the debugger.
 * The parameter `ui` contains a reference to the gui object, which
 * in turn contains a reference to the debugger object via `ui->arg`.
 * Returns TRUE on success.
 */
int debugger_debug(struct gui *ui);


#endif /* __DEBUGGER_DEBUGGER */
