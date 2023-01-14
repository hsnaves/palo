
#include <stddef.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "debugger/debugger.h"
#include "simulator/simulator.h"
#include "gui/gui.h"
#include "simulator/disk.h"
#include "simulator/display.h"
#include "simulator/ethernet.h"
#include "simulator/intr.h"
#include "microcode/microcode.h"
#include "microcode/nova.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Functions. */

/* Gets a command line from the standard input.
 * The command line is stored in `dbg->cmd_buf`, with the words separated by a
 * NUL character. The last word is ended with two consecutive NUL characters.
 * The parameter `is_eof` returns TRUE if it reached the EOF.
 */
static
void get_command(struct debugger *dbg, int *is_eof)
{
    size_t i, len;
    int c, last_is_space;

    *is_eof = FALSE;
    printf(">");

    i = len = 0;
    last_is_space = TRUE;
    while (TRUE) {
        c = fgetc(stdin);
        if (c == EOF) {
            *is_eof = TRUE;
            return;
        }
        if (c == '\n') break;

        if (isspace(c)) {
            if (last_is_space) continue;
            last_is_space = TRUE;
            c = '\0';
        } else {
            last_is_space = FALSE;
        }

        if (i + 2 < dbg->cmd_buf_size)
            dbg->cmd_buf[i++] = (char) c;
        len++;
    }

    /* Return the same command as before. */
    if (i == 0) return;

    if (!last_is_space) {
        dbg->cmd_buf[i++] = '\0';
        len++;
    }
    dbg->cmd_buf[i++] = '\0';
    len++;

    if (len >= dbg->cmd_buf_size) {
        printf("command too long\n");
        dbg->cmd_buf[0] = '\0';
        dbg->cmd_buf[1] = '\0';
    }
}

/* Runs the simulation.
 * The parameter `max_steps` specifies the maximum number of steps
 * to run. If `max_steps` is negative, it runs indefinitely. Similarly,
 * `max_cycles` specifies the maximum number of cycles to run.
 * Returns TRUE on success.
 */
static
int simulate(struct debugger *dbg, int max_steps, int max_cycles)
{
    const struct breakpoint *bp;
    struct gui *ui;
    struct simulator *sim;
    unsigned int num, max_breakpoints;
    uint32_t prev_cycle;
    int step, cycle, hit, hit1;
    int running, stop_sim;

    /* Get the effective number of breakpoints. */
    max_breakpoints = 0;
    for (num = 0; num < dbg->max_breakpoints; num++) {
        bp = &dbg->bps[num];
        if (!bp->available && bp->enable) {
            max_breakpoints = num + 1;
        }
    }

    ui = dbg->ui;
    sim = dbg->sim;

    step = 0;
    cycle = 0;
    running = TRUE;
    stop_sim = FALSE;
    while (TRUE) {
        if (max_steps >= 0 && step == max_steps)
            break;
        if (max_cycles >= 0 && cycle >= max_cycles)
            break;

        if (sim->error) break;

        prev_cycle = sim->cycle;
        simulator_step(sim);
        cycle += (int) (INTR_CYCLE(sim->cycle - prev_cycle));
        step++;

        if ((step % 100000) == 0) {
            if (unlikely(!gui_running(ui, &running, &stop_sim))) {
                report_error("debugger: simulate: "
                             "could not determine if GUI is running");
                return FALSE;
            }
            if (!running || stop_sim) break;

            if (unlikely(!gui_update(ui))) {
                report_error("debugger: simulate: "
                             "could not update GUI");
                return FALSE;
            }
            if (unlikely(!gui_wait_frame(ui))) {
                report_error("debugger: simulate: "
                             "could not wait for next frame");
                return FALSE;
            }
        }

        /* Small optimization. */
        if (max_breakpoints == 0) continue;

        hit = FALSE;
        for (num = 0; num < max_breakpoints; num++) {
            bp = &dbg->bps[num];
            if (!bp->enable) continue;

            hit1 = TRUE;
            if (bp->task != 0xFF) {
                if (bp->task != sim->ctask)
                    hit1 = FALSE;
            }

            if (bp->ntask != 0xFF) {
                if (bp->ntask != sim->ntask)
                    hit1 = FALSE;
            }

            if (bp->mpc != 0xFFFF) {
                if (bp->mpc != sim->mpc)
                    hit1 = FALSE;
            }

            if (bp->on_task_switch) {
                if (!sim->task_switch)
                    hit1 = FALSE;
            }

            if (bp->mir_mask != 0) {
                if ((sim->mir & bp->mir_mask) != bp->mir_fmt)
                    hit1 = FALSE;
                if (!bp->allow_constants) {
                    if ((MICROCODE_F1(sim->mir) == F1_CONSTANT)
                        || (MICROCODE_F2(sim->mir) == F2_CONSTANT)) {
                        hit1 = FALSE;
                    }
                }
            }

            if (bp->watch) {
                if (bp->addr != sim->mar) {
                    hit1 = FALSE;
                }
            }

            if (hit1) {
                if (num > 0) {
                    printf("breakpoint %u hit\n", num);
                }
                hit = TRUE;
                break;
            }
        }

        if (hit) break;
    }

    return TRUE;
}

/* Processes the registers command.
 * If the `extra` parameter is set to TRUE, it prints the extra
 * registers instead.
 */
static
void cmd_registers(struct debugger *dbg, int extra)
{
    struct decoder *dec;

    debugger_disassemble(dbg);
    printf("%s\n", string_buffer_string(&dbg->output));

    dec = debugger_setup_decoder(dbg);
    if (extra) {
        simulator_print_extra_registers(dbg->sim, dec);
    } else {
        simulator_print_registers(dbg->sim, dec);
    }
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Prints the nova registers. */
static
void cmd_nova_registers(struct debugger *dbg)
{
    struct decoder *dec;

    debugger_nova_disassemble(dbg);
    printf("%s\n", string_buffer_string(&dbg->output));

    dec = debugger_setup_decoder(dbg);
    simulator_print_nova_registers(dbg->sim, dec);
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Shows the disk registers. */
static
void cmd_disk_registers(struct debugger *dbg)
{
    struct decoder *dec;
    dec = debugger_setup_decoder(dbg);
    disk_print_registers(&dbg->sim->dsk, dec);
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Shows the display registers. */
static
void cmd_display_registers(struct debugger *dbg)
{
    struct decoder *dec;
    dec = debugger_setup_decoder(dbg);
    display_print_registers(&dbg->sim->displ, dec);
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Shows the ethernet registers. */
static
void cmd_ethernet_registers(struct debugger *dbg)
{
    struct decoder *dec;
    dec = debugger_setup_decoder(dbg);
    ethernet_print_registers(&dbg->sim->ether, dec);
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Shows the keyboard registers. */
static
void cmd_keyboard_registers(struct debugger *dbg)
{
    struct decoder *dec;
    dec = debugger_setup_decoder(dbg);
    keyboard_print_registers(&dbg->sim->keyb, dec);
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Shows the mouse registers. */
static
void cmd_mouse_registers(struct debugger *dbg)
{
    struct decoder *dec;
    dec = debugger_setup_decoder(dbg);
    mouse_print_registers(&dbg->sim->mous, dec);
    printf("%s\n", string_buffer_string(&dbg->output));
}

/* Dumps the contents of memory.
 * Returns TRUE on success.
 */
static
int cmd_dump_memory(struct debugger *dbg)
{
    struct simulator *sim;
    struct gui *ui;
    const char *arg, *end;
    uint16_t addr, num, val;
    int running, stop_sim;

    sim = dbg->sim;
    ui = dbg->ui;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    num = 8;
    if (arg[0] != '\0') {
        addr = (uint16_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid address (octal number) %s\n", arg);
            return TRUE;
        }
        arg = &arg[strlen(arg) + 1];

        if (arg[0] != '\0') {
            num = (uint16_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid octal number %s\n", arg);
                return TRUE;
            }
        }
    } else {
        addr = 0;
    }

    running = TRUE;
    stop_sim = FALSE;
    while (num-- > 0) {
        if (unlikely(!gui_running(ui, &running, &stop_sim))) {
            report_error("debugger: cmd_dump_memory: "
                         "could not determine if GUI is running");
            return FALSE;
        }
        if (!running || stop_sim) break;
        val = simulator_read(sim, addr, sim->ctask, FALSE);
        printf("%06o: %06o\n", addr++, val);
    }

    return TRUE;
}

/* Writes to memory. */
static
void cmd_write_memory(struct debugger *dbg)
{
    struct simulator *sim;
    const char *arg, *end;
    uint16_t addr, val;

    sim = dbg->sim;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] == '\0') {
        printf("please specify the address and the value\n");
        return;
    }

    addr = (uint16_t) strtoul(arg, (char **) &end, 8);
    if (end[0] != '\0') {
        printf("invalid address (octal number) %s\n", arg);
        return;
    }

    arg = &arg[strlen(arg) + 1];
    if (arg[0] == '\0') {
        printf("please specify the value to write\n");
        return;
    }

    val = (uint16_t) strtoul(arg, (char **) &end, 8);
    if (end[0] != '\0') {
        printf("invalid value (octal number) %s\n", arg);
    }

    simulator_write(sim, addr, val, sim->ctask, FALSE);
}

/* Processes the continue command.
 * Returns TRUE on success.
 */
static
int cmd_continue(struct debugger *dbg)
{
    dbg->bps[0].enable = FALSE;
    if (unlikely(!simulate(dbg, -1, -1))) {
        report_error("debugger: cmd_continue: could not simulate");
        return FALSE;
    }

    cmd_registers(dbg, FALSE);
    return TRUE;
}

/* Processes the "next" command.
 * Returns TRUE on success.
 */
static
int cmd_next(struct debugger *dbg)
{
    const char *arg, *end;
    int num;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] != '\0') {
        num = (int) strtoul(arg, (char **) &end, 10);
        if (end[0] != '\0' || num < 0) {
            printf("invalid number %s\n", arg);
            return TRUE;
        }
    } else {
        num = 1;
    }

    dbg->bps[0].enable = FALSE;
    if (unlikely(!simulate(dbg, num, -1))) {
        report_error("debugger: cmd_next: could not simulate");
        return FALSE;
    }

    cmd_registers(dbg, FALSE);
    return TRUE;
}

/* Processes the "step" command.
 * Returns TRUE on success.
 */
static
int cmd_step(struct debugger *dbg)
{
    const char *arg, *end;
    int num;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] != '\0') {
        num = (int) strtoul(arg, (char **) &end, 10);
        if (end[0] != '\0' || num < 0) {
            printf("invalid number %s\n", arg);
            return TRUE;
        }
    } else {
        num = 1;
    }

    dbg->bps[0].enable = FALSE;
    if (unlikely(!simulate(dbg, -1, num))) {
        report_error("debugger: cmd_step: could not simulate");
        return FALSE;
    }

    cmd_registers(dbg, FALSE);
    return TRUE;
}

/* Processes the "next task" command.
 * Returns TRUE on success.
 */
int cmd_next_task(struct debugger *dbg)
{
    struct breakpoint *bp;
    const char *arg, *end;
    uint8_t task;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] != '\0') {
        task = (uint8_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid task (octal number) %s\n", arg);
            return TRUE;
        }
    } else {
        task = 0xFF;
    }

    bp = &dbg->bps[0];
    bp->enable = TRUE;
    bp->task = task;
    bp->ntask = 0xFF;
    bp->mpc = 0xFFFF;
    bp->on_task_switch = TRUE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;
    bp->allow_constants = TRUE;
    bp->addr = 0;
    bp->watch = FALSE;

    if (unlikely(!simulate(dbg, -1, -1))) {
        report_error("debugger: cmd_next_task: could not simulate");
        return FALSE;
    }

    cmd_registers(dbg, FALSE);
    return TRUE;
}

/* Processes the "next nova" command.
 * Returns TRUE on success.
 */
int cmd_next_nova(struct debugger *dbg)
{
    struct simulator *sim;
    struct gui *ui;
    struct breakpoint *bp;
    const char *arg, *end;
    int running, stop_sim;
    int num;

    sim = dbg->sim;
    ui = dbg->ui;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] != '\0') {
        num = (int) strtoul(arg, (char **) &end, 10);
        if (end[0] != '\0' || num < 0) {
            printf("invalid number %s\n", arg);
            return TRUE;
        }
    } else {
        num = 1;
    }

    bp = &dbg->bps[0];
    bp->enable = TRUE;
    bp->task = TASK_EMULATOR;
    bp->ntask = 0xFF;
    bp->mpc = 020;
    bp->on_task_switch = FALSE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;
    bp->allow_constants = TRUE;
    bp->addr = 0;
    bp->watch = FALSE;

    running = TRUE;
    stop_sim = FALSE;
    while (num-- > 0) {
        if (unlikely(!gui_running(ui, &running, &stop_sim))) {
            report_error("debugger: cmd_next_nova: "
                         "could not determine if GUI is running");
            return FALSE;
        }
        if (!running || stop_sim) break;
        if (sim->error) break;
        if (unlikely(!simulate(dbg, -1, -1))) {
            report_error("debugger: cmd_next_nova: could not simulate");
            return FALSE;
        }
    }

    cmd_nova_registers(dbg);
    return TRUE;
}

/* Adds a breakpoint based on the string in the command buffer. */
static
void cmd_add_breakpoint(struct debugger *dbg)
{
    const char *arg, *end;
    struct breakpoint *bp;
    uint32_t mask, val;
    unsigned int num;

    for (num = 1; num < dbg->max_breakpoints; num++) {
        if (dbg->bps[num].available) break;
    }
    if (num >= dbg->max_breakpoints) {
        printf("maximum number of breakpoints reached\n");
        return;
    }

    bp = &dbg->bps[num];
    bp->enable = FALSE;
    bp->task = 0xFF;
    bp->ntask = 0xFF;
    bp->mpc = 0xFFFF;
    bp->on_task_switch = FALSE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;
    bp->allow_constants = TRUE;
    bp->addr = 0;
    bp->watch = FALSE;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    while (arg[0] != '\0') {
        if (strcmp(arg, "-task") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the task\n");
                return;
            }
            bp->task = (uint8_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid task (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-ntask") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the ntask\n");
                return;
            }
            bp->ntask = (uint8_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid ntask (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-on_task_switch") == 0) {
            arg = &arg[strlen(arg) + 1];
            bp->on_task_switch = TRUE;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-mir") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the MIR format\n");
                return;
            }
            bp->mir_fmt = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid MIR format (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the MIR mask\n");
                return;
            }
            bp->mir_mask = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid MIR mask (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-rsel") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the RSEL value\n");
                return;
            }
            val = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid RSEL (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];

            mask = MC_RSEL_M << MC_RSEL_S;
            bp->mir_mask |= mask;
            bp->mir_fmt &= ~mask;
            bp->mir_fmt |= val << MC_RSEL_S;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-aluf") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the ALUF value\n");
                return;
            }
            val = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid ALUF (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];

            mask = MC_ALUF_M << MC_ALUF_S;
            bp->mir_mask |= mask;
            bp->mir_fmt &= ~mask;
            bp->mir_fmt |= val << MC_ALUF_S;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-bs") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the BS value\n");
                return;
            }
            val = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid BS (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];

            mask = MC_BS_M << MC_BS_S;
            bp->mir_mask |= mask;
            bp->mir_fmt &= ~mask;
            bp->mir_fmt |= val << MC_BS_S;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-f1") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the F1 value\n");
                return;
            }
            val = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid F1 (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];

            mask = MC_F1_M << MC_F1_S;
            bp->mir_mask |= mask;
            bp->mir_fmt &= ~mask;
            bp->mir_fmt |= val << MC_F1_S;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-f2") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the F2 value\n");
                return;
            }
            val = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid F2 (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];

            mask = MC_F2_M << MC_F2_S;
            bp->mir_mask |= mask;
            bp->mir_fmt &= ~mask;
            bp->mir_fmt |= val << MC_F2_S;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-store") == 0) {
            arg = &arg[strlen(arg) + 1];

            mask = MC_F2_M << MC_F2_S;
            bp->mir_mask |= mask;
            bp->mir_fmt &= ~mask;
            bp->mir_fmt |= (F2_STORE_MD) << MC_F2_S;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-no_constants") == 0) {
            arg = &arg[strlen(arg) + 1];

            bp->allow_constants = FALSE;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-watch") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the watch address\n");
                return;
            }
            bp->addr = (uint16_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid address (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->watch = TRUE;
            bp->enable = TRUE;
            continue;
        }

        bp->mpc = (uint16_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid MPC (octal number) %s\n", arg);
            return;
        }
        arg = &arg[strlen(arg) + 1];
        bp->enable = TRUE;
    }

    if (!bp->enable) {
        printf("no breakpoint defined\n");
        return;
    }

    bp->available = FALSE;
    printf("breakpoint %u created\n", num);
}

/* Lists the breakpoints. */
static
void cmd_breakpoint_list(struct debugger *dbg)
{
    unsigned int num;
    struct breakpoint *bp;

    printf("NUM  EN  TASK  NTASK MPC     SW  MIR_FMT     "
           "MIR_MASK    CT  ADDR\n");
    for (num = 1; num < dbg->max_breakpoints; num++) {
        bp = &dbg->bps[num];
        if (bp->available) continue;

        printf("%-4d %o   %03o   %03o   %06o  %o   "
               "%011o %011o %o   %06o%s\n",
               num, bp->enable ? 1 : 0,
               bp->task, bp->ntask, bp->mpc,
               bp->on_task_switch ? 1 : 0,
               bp->mir_fmt, bp->mir_mask,
               bp->allow_constants ? 1 : 0,
               bp->addr, bp->watch ? "*" : " ");
    }
}

/* Enables or disables a breakpoint based on the parameter `enable`. */
static
void cmd_breakpoint_enable(struct debugger *dbg, int enable)
{
    const char *arg, *end;
    struct breakpoint *bp;
    unsigned int num;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] == '\0') {
        printf("please specify a breakpoint number\n");
        return;
    }

    num = strtoul(arg, (char **) &end, 10);
    if (end[0] != '\0' || num == 0) {
        printf("invalid breakpoint number %s\n", arg);
        return;
    }

    if (num >= dbg->max_breakpoints) {
        printf("breakpoint number exceeds maximum available\n");
        return;
    }

    bp = &dbg->bps[num];
    bp->enable = enable;
    printf("breakpoint %u %s\n",
           num, (enable) ? "enabled" : "disabled");
}

/* Removes a breakpoint. */
static
void cmd_breakpoint_remove(struct debugger *dbg)
{
    struct breakpoint *bp;
    const char *arg, *end;
    unsigned int num;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] == '\0') {
        printf("please specify a breakpoint number\n");
        return;
    }

    num = strtoul(arg, (char **) &end, 10);
    if (end[0] != '\0' || num == 0) {
        printf("invalid breakpoint number %s\n", arg);
        return;
    }

    if (num >= dbg->max_breakpoints) {
        printf("breakpoint number exceeds maximum available\n");
        return;
    }

    bp = &dbg->bps[num];
    if (bp->available) {
        printf("breakpoint %u is available\n", num);
    } else {
        dbg->bps[num].available = TRUE;
        printf("breakpoint %u removed\n", num);
    }
}

/* Loads or saves a disk image.
 * The parameter `save` is set to TRUE for when saving the image.
 */
static
void cmd_load_or_save_image(struct debugger *dbg, int save)
{
    struct simulator *sim;
    const char *arg, *end;
    const char *filename;
    unsigned int drive_num;

    sim = dbg->sim;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] == '\0') {
        printf("please specify a drive number and a filename\n");
        return;
    }

    drive_num = strtoul(arg, (char **) &end, 10);
    if (end[0] != '\0') {
        printf("invalid drive number %s\n", arg);
        return;
    }

    if (drive_num >= NUM_DISK_DRIVES) {
        printf("drive number too large\n");
        return;
    }

    arg = &arg[strlen(arg) + 1];
    if (arg[0] == '\0') {
        printf("please specify a filename\n");
        return;
    }
    filename = arg;

    if (save) {
        disk_save_image(&sim->dsk, drive_num, filename);
    } else {
        disk_load_image(&sim->dsk, drive_num, filename);
    }
}

/* Loads or saves the simulator state.
 * The parameter `save` is set to TRUE for when saving the state.
 */
static
void cmd_load_or_save_state(struct debugger *dbg, int save)
{
    struct simulator *sim;
    const char *arg;
    const char *filename;

    sim = dbg->sim;

    arg = (const char *) dbg->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] == '\0') {
        printf("please specify a filename\n");
        return;
    }
    filename = arg;

    if (save) {
        simulator_save_state(sim, filename);
    } else {
        simulator_load_state(sim, filename);
    }
}

/* Restarts the simulation. */
static
void cmd_restart(struct debugger *dbg)
{
    struct simulator *sim;
    sim = dbg->sim;
    simulator_reset(sim);
    cmd_registers(dbg, FALSE);
}

/* Prints the help. */
static
void cmd_help(struct debugger *dbg)
{
    UNUSED(dbg);

    printf("Commands:\n");
    printf("  oct              Use octal numbers\n");
    printf("  hex              Use hexadecimal numbers\n");
    printf("  r                Print the registers\n");
    printf("  nr               Print the NOVA registers\n");
    printf("  e                Print the extra registers\n");
    printf("  dsk              Print the disk registers\n");
    printf("  displ            Print the display registers\n");
    printf("  ether            Print the ethernet registers\n");
    printf("  keyb             Print the keyboard registers\n");
    printf("  mous             Print the mouse registers\n");
    printf("  d [addr] [num]   Dump the memory contents\n");
    printf("  w addr val       Writes a word to memory\n");
    printf("  c                Continue execution\n");
    printf("  n [num]          Step through the microcode\n");
    printf("  s [cycles]       Step through the microcode\n");
    printf("  nt [task]        Step until switch task\n");
    printf("  nn [num]         Execute nova instructions\n");
    printf("  bp specs         Add a breakpoint\n");
    printf("  bl               List breakpoints\n");
    printf("  be num           Enable a breakpoint\n");
    printf("  bd num           Disable a breakpoint\n");
    printf("  br num           Remove a breakpoint\n");
    printf("  li num file      Load a disk drive image\n");
    printf("  si num file      Save a disk drive image\n");
    printf("  ls file          Load the simulator state\n");
    printf("  ss file          Save the simulator state\n");
    printf("  zs               Restart the simulation\n");
    printf("  h                Print this help\n");
    printf("  q                Quit the debugger\n");
    printf("\n");
    printf("The specifications of the breakpoints are:\n");
    printf("  bp [options] mpc\n\n");
    printf("where the options are:\n");
    printf("  -task <task>     To specify the current task\n");
    printf("  -ntask <ntask>   To specify the next task\n");
    printf("  -on_task_switch  When a task switch occurs\n");
    printf("  -mir fmt mask    To filter based on the MIR\n");
    printf("  -rsel rsel       To select the RSEL of the MIR\n");
    printf("  -aluf aluf       To select the ALUF of the MIR\n");
    printf("  -bs bs           To select the BS of the MIR\n");
    printf("  -f1 f1           To select the F1 of the MIR\n");
    printf("  -f2 f2           To select the F2 of the MIR\n");
    printf("  -store           When F2=F2_STORE_MD\n");
    printf("  -no_constants    To disable F1 or F2 constants\n");
    printf("  -watch address   To watch for memory activity\n");
}

int debugger_debug(struct gui *ui)
{
    struct debugger *dbg;
    const char *cmd;
    int running, stop_sim;
    int is_eof, ret;

    dbg = (struct debugger *) ui->arg;
    ret = TRUE;

    if (!dbg->use_debugger) {
        if (unlikely(!simulate(dbg, -1, -1))) {
            report_error("debugger: debug: could not simulate");
            ret = FALSE;
        }
        goto do_exit;
    }

    dbg->bps[0].available = FALSE;
    dbg->cmd_buf[0] = '\0';
    dbg->cmd_buf[1] = '\0';

    running = TRUE;
    stop_sim = FALSE;
    while (TRUE) {
        if (unlikely(!gui_update(ui))) {
            report_error("debugger: debug: could not update GUI");
            ret = FALSE;
            goto do_exit;
        }

        if (unlikely(!gui_running(ui, &running, NULL))) {
            report_error("debugger: debug: "
                         "could not determine if GUI is running");
            ret = FALSE;
            goto do_exit;
        }
        if (!running) break;

        get_command(dbg, &is_eof);
        if (is_eof) break;

        if (unlikely(!gui_running(ui, &running, &stop_sim))) {
            report_error("debugger: debug: "
                         "could not determine if GUI is running");
            ret = FALSE;
            goto do_exit;
        }
        if (!running) break;

        cmd = (const char *) dbg->cmd_buf;

        if (strcmp(cmd, "oct") == 0) {
            dbg->use_octal = TRUE;
            continue;
        }

        if (strcmp(cmd, "hex") == 0) {
            dbg->use_octal = FALSE;
            continue;
        }

        if (strcmp(cmd, "r") == 0) {
            cmd_registers(dbg, FALSE);
            continue;
        }

        if (strcmp(cmd, "nr") == 0) {
            cmd_nova_registers(dbg);
            continue;
        }

        if (strcmp(cmd, "e") == 0) {
            cmd_registers(dbg, TRUE);
            continue;
        }

        if (strcmp(cmd, "dsk") == 0) {
            cmd_disk_registers(dbg);
            continue;
        }

        if (strcmp(cmd, "displ") == 0) {
            cmd_display_registers(dbg);
            continue;
        }

        if (strcmp(cmd, "ether") == 0) {
            cmd_ethernet_registers(dbg);
            continue;
        }

        if (strcmp(cmd, "keyb") == 0) {
            cmd_keyboard_registers(dbg);
            continue;
        }

        if (strcmp(cmd, "mous") == 0) {
            cmd_mouse_registers(dbg);
            continue;
        }

        if (strcmp(cmd, "d") == 0) {
            if (unlikely(!cmd_dump_memory(dbg))) {
                ret = FALSE;
                goto do_exit;
            }
            continue;
        }

        if (strcmp(cmd, "w") == 0) {
            cmd_write_memory(dbg);
            continue;
        }

        if (strcmp(cmd, "c") == 0) {
            if (unlikely(!cmd_continue(dbg))) {
                ret = FALSE;
                goto do_exit;
            }
            continue;
        }

        if (strcmp(cmd, "n") == 0) {
            if (unlikely(!cmd_next(dbg))) {
                ret = FALSE;
                goto do_exit;
            }
            continue;
        }

        if (strcmp(cmd, "s") == 0) {
            if (unlikely(!cmd_step(dbg))) {
                ret = FALSE;
                goto do_exit;
            }
            continue;
        }

        if (strcmp(cmd, "nt") == 0) {
            if (unlikely(!cmd_next_task(dbg))) {
                ret = FALSE;
                goto do_exit;
            }
            continue;
        }

        if (strcmp(cmd, "nn") == 0) {
            if (unlikely(!cmd_next_nova(dbg))) {
                ret = FALSE;
                goto do_exit;
            }
            continue;
        }

        if (strcmp(cmd, "bp") == 0) {
            cmd_add_breakpoint(dbg);
            continue;
        }

        if (strcmp(cmd, "bl") == 0) {
            cmd_breakpoint_list(dbg);
            continue;
        }

        if (strcmp(cmd, "be") == 0) {
            cmd_breakpoint_enable(dbg, TRUE);
            continue;
        }

        if (strcmp(cmd, "bd") == 0) {
            cmd_breakpoint_enable(dbg, FALSE);
            continue;
        }

        if (strcmp(cmd, "br") == 0) {
            cmd_breakpoint_remove(dbg);
            continue;
        }

        if (strcmp(cmd, "li") == 0) {
            cmd_load_or_save_image(dbg, FALSE);
            continue;
        }

        if (strcmp(cmd, "si") == 0) {
            cmd_load_or_save_image(dbg, TRUE);
            continue;
        }

        if (strcmp(cmd, "ls") == 0) {
            cmd_load_or_save_state(dbg, FALSE);
            continue;
        }

        if (strcmp(cmd, "ss") == 0) {
            cmd_load_or_save_state(dbg, TRUE);
            continue;
        }

        if (strcmp(cmd, "zs") == 0) {
            cmd_restart(dbg);
            continue;
        }

        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            cmd_help(dbg);
            continue;
        }

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0)
            break;

        printf("invalid command\n");
        dbg->cmd_buf[0] = '\0';
        dbg->cmd_buf[1] = '\0';
    }

do_exit:
    if (unlikely(!gui_stop(ui))) {
        report_error("debugger: debug: could not stop GUI");
        return FALSE;
    }

    return ret;
}
