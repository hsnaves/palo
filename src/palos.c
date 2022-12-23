
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "simulator/simulator.h"
#include "simulator/disk.h"
#include "simulator/display.h"
#include "simulator/ethernet.h"
#include "simulator/utils.h"
#include "microcode/microcode.h"
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
struct palos {
    const char *const_filename;   /* The name of the constant rom. */
    const char *mcode_filename;   /* The name of the microcode rom. */
    const char *disk1_filename;   /* Disk 1 image file. */
    const char *disk2_filename;   /* Disk 2 image file. */

    struct gui ui;                /* The user input. */
    struct simulator sim;         /* The simulator. */

    size_t max_breakpoints;       /* The maximum number of breakpoints. */
    struct breakpoint *bps;       /* The breakpoints. */

    char *cmd_buf;                /* Buffer for command. */
    size_t cmd_buf_size;          /* Size of the command buffer. */

    char *out_buf;                /* Buffer for output. */
    size_t out_buf_size;          /* Size of the output buffer. */
    struct string_buffer output;  /* The string buffer for output. */
};

/* Forward declarations. */
static int palos_debug(struct gui *ui);

/* Functions. */

/* Initializes the palos variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
static
void palos_initvar(struct palos *ps)
{
    simulator_initvar(&ps->sim);
    gui_initvar(&ps->ui);

    ps->bps = NULL;
    ps->cmd_buf = NULL;
    ps->out_buf = NULL;
}

/* Destroys the palos object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
static
void palos_destroy(struct palos *ps)
{
    if (ps->bps) free((void *) ps->bps);
    ps->bps = NULL;

    if (ps->cmd_buf) free((void *) ps->cmd_buf);
    ps->cmd_buf = NULL;

    if (ps->out_buf) free((void *) ps->out_buf);
    ps->out_buf = NULL;

    gui_destroy(&ps->ui);
    simulator_destroy(&ps->sim);
}

/* Creates a new palos object.
 * This obeys the initvar / destroy / create protocol.
 * The `sys_type` variable specifies the system type.
 * The name of the several filenames to load related to the constant rom,
 * microcode rom, and disk images are given by the parameters:
 * `const_filename`, `mcode_filename`, `disk1_filename`, and
 * `disk2_filename`, respectively.
 * Returns TRUE on success.
 */
static
int palos_create(struct palos *ps,
                 enum system_type sys_type,
                 const char *const_filename,
                 const char *mcode_filename,
                 const char *disk1_filename,
                 const char *disk2_filename)
{
    palos_initvar(ps);

    ps->max_breakpoints = 1024;
    ps->cmd_buf_size = 8192;
    ps->out_buf_size = 8192;

    ps->bps = (struct breakpoint *)
        malloc(ps->max_breakpoints * sizeof(struct breakpoint));
    ps->cmd_buf = (char *) malloc(ps->cmd_buf_size);
    ps->out_buf = (char *) malloc(ps->out_buf_size);

    if (unlikely(!ps->bps || !ps->cmd_buf || !ps->out_buf)) {
        report_error("palos: create: memory exhausted");
        palos_destroy(ps);
        return FALSE;
    }

    if (unlikely(!simulator_create(&ps->sim, sys_type))) {
        report_error("palos: create: could not create simulator");
        palos_destroy(ps);
        return FALSE;
    }

    if (unlikely(!gui_create(&ps->ui, &ps->sim, &palos_debug, ps))) {
        report_error("palos: create: could not create user interface");
        return FALSE;
    }

    ps->const_filename = const_filename;
    ps->mcode_filename = mcode_filename;
    ps->disk1_filename = disk1_filename;
    ps->disk2_filename = disk2_filename;

    ps->output.buf = ps->out_buf;
    ps->output.buf_size = ps->out_buf_size;

    return TRUE;
}

/* Runs the PALOS simulator.
 * Returns TRUE on success.
 */
static
int palos_run(struct palos *ps)
{
    const char *fn;

    fn = ps->const_filename;
    if (unlikely(!simulator_load_constant_rom(&ps->sim, fn))) {
        report_error("palos: run: could not load constant rom");
        return FALSE;
    }

    fn = ps->mcode_filename;
    if (unlikely(!simulator_load_microcode_rom(&ps->sim, fn, 0))) {
        report_error("palos: run: could not load microcode rom");
        return FALSE;
    }

    fn = ps->disk1_filename;
    if (fn) {
        if (unlikely(!disk_load_image(&ps->sim.dsk, 0, fn))) {
            report_error("palos: run: could not load disk 1");
            return FALSE;
        }
    }

    fn = ps->disk2_filename;
    if (fn) {
        if (unlikely(!disk_load_image(&ps->sim.dsk, 1, fn))) {
            report_error("palos: run: could not load disk 2");
            return FALSE;
        }
    }

    simulator_reset(&ps->sim);

    if (unlikely(!gui_start(&ps->ui))) {
        report_error("palos: run: could not start user interface");
        return FALSE;
    }

    return TRUE;
}

/* Gets a command line from the standard input.
 * The command line is stored in `ps->cmd_buf`, with the words separated by a
 * NUL character. The last word is ended with two consecutive NUL characters.
 */
static
void palos_get_command(struct palos *ps)
{
    size_t i, len;
    int c, last_is_space;

    printf(">");

    i = len = 0;
    last_is_space = TRUE;
    while (TRUE) {
        c = fgetc(stdin);
        if (c == EOF) break;
        if (c == '\n') break;

        if (isspace(c)) {
            if (last_is_space) continue;
            last_is_space = TRUE;
            c = '\0';
        } else {
            last_is_space = FALSE;
        }

        if (i + 2 < ps->cmd_buf_size)
            ps->cmd_buf[i++] = (char) c;
        len++;
    }

    /* Return the same command as before. */
    if (i == 0) return;

    if (!last_is_space) {
        ps->cmd_buf[i++] = '\0';
        len++;
    }
    ps->cmd_buf[i++] = '\0';
    len++;

    if (len >= ps->cmd_buf_size) {
        printf("command too long\n");
        ps->cmd_buf[0] = '\0';
        ps->cmd_buf[1] = '\0';
    }
}

/* Runs the simulation.
 * The parameter `max_steps` specifies the maximum number of steps
 * to run. If `max_steps` is negative, it runs indefinitely. Similarly,
 * `max_cycles` specifies the maximum number of cycles to run.
 * Returns TRUE on success.
 */
static
int palos_simulate(struct palos *ps, int max_steps, int max_cycles)
{
    const struct breakpoint *bp;
    unsigned int num, max_breakpoints;
    int step, cycle, hit, hit1;
    uint32_t prev_cycle;

    /* Get the effective number of breakpoints. */
    max_breakpoints = 0;
    for (num = 0; num < ps->max_breakpoints; num++) {
        bp = &ps->bps[num];
        if (!bp->available && bp->enable) {
            max_breakpoints = num + 1;
        }
    }

    step = 0;
    cycle = 0;
    while (gui_running(&ps->ui)) {
        if (max_steps >= 0 && step == max_steps)
            break;
        if (max_cycles >=0 && cycle >= max_cycles)
            break;

        if (ps->sim.error) break;

        prev_cycle = ps->sim.cycle;
        simulator_step(&ps->sim);
        cycle += (int) (INTR_CYCLE(ps->sim.cycle - prev_cycle));
        step++;

        if ((step % 100000) == 0) {
            if (unlikely(!gui_update(&ps->ui))) {
                report_error("palos: simulate: "
                             "could not update GUI");
                return FALSE;
            }
            if (unlikely(!gui_wait_frame(&ps->ui))) {
                report_error("palos: simulate: "
                             "could not wait for next frame");
                return FALSE;
            }
        }

        hit = FALSE;
        for (num = 0; num < max_breakpoints; num++) {
            bp = &ps->bps[num];
            if (!bp->enable) continue;

            hit1 = TRUE;
            if (bp->task != 0xFF) {
                if (bp->task != ps->sim.ctask)
                    hit1 = FALSE;
            }

            if (bp->ntask != 0xFF) {
                if (bp->ntask != ps->sim.ntask)
                    hit1 = FALSE;
            }

            if (bp->mpc != 0xFFFF) {
                if (bp->mpc != ps->sim.mpc)
                    hit1 = FALSE;
            }

            if (bp->on_task_switch) {
                if (!ps->sim.task_switch)
                    hit1 = FALSE;
            }

            if (bp->mir_mask != 0) {
                if ((ps->sim.mir & bp->mir_mask) != bp->mir_fmt)
                    hit1 = FALSE;
                if (!bp->allow_constants) {
                    if ((MICROCODE_F1(ps->sim.mir) == F1_CONSTANT)
                        || (MICROCODE_F2(ps->sim.mir) == F2_CONSTANT)) {
                        hit1 = FALSE;
                    }
                }
            }

            if (bp->r_watch) {
                if (!ps->sim.r_changed)
                    hit1 = FALSE;
                if (bp->rsel != ps->sim.modified_rsel)
                    hit1 = FALSE;
            }

            if (bp->watch) {
                if (bp->addr != ps->sim.mar) {
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
void palos_cmd_registers(struct palos *ps, int extra)
{
    string_buffer_reset(&ps->output);
    simulator_disassemble(&ps->sim, &ps->output);
    printf("%s\n", ps->out_buf);

    string_buffer_reset(&ps->output);
    if (extra) {
        simulator_print_extra_registers(&ps->sim, &ps->output);
    } else {
        simulator_print_registers(&ps->sim, &ps->output);
    }
    printf("%s\n", ps->out_buf);
}

/* Prints the nova registers. */
static
void palos_cmd_nova_registers(struct palos *ps)
{
    string_buffer_reset(&ps->output);
    simulator_nova_disassemble(&ps->sim, &ps->output);
    printf("%s\n", ps->out_buf);

    string_buffer_reset(&ps->output);
    simulator_print_nova_registers(&ps->sim, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Shows the disk registers. */
static
void palos_cmd_disk_registers(struct palos *ps)
{
    string_buffer_reset(&ps->output);
    disk_print_registers(&ps->sim.dsk, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Shows the display registers. */
static
void palos_cmd_display_registers(struct palos *ps)
{
    string_buffer_reset(&ps->output);
    display_print_registers(&ps->sim.displ, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Shows the ethernet registers. */
static
void palos_cmd_ethernet_registers(struct palos *ps)
{
    string_buffer_reset(&ps->output);
    ethernet_print_registers(&ps->sim.ether, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Dumps the contents of memory. */
static
void palos_cmd_dump_memory(struct palos *ps)
{
    const char *arg, *end;
    uint16_t addr, num, val;

    arg = (const char *) ps->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    num = 8;
    if (arg[0] != '\0') {
        addr = (uint16_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid address (octal number) %s\n", arg);
            return;
        }
        arg = &arg[strlen(arg) + 1];

        if (arg[0] != '\0') {
            num = (uint16_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid octal number %s\n", arg);
            }
        }
    } else {
        addr = 0;
    }

    while ((num-- > 0) && gui_running(&ps->ui)) {
        val = simulator_read(&ps->sim, addr, ps->sim.ctask, FALSE);
        printf("%06o: %06o\n", addr++, val);
    }
}

/* Writes to memory. */
static
void palos_cmd_write_memory(struct palos *ps)
{
    const char *arg, *end;
    uint16_t addr, val;

    arg = (const char *) ps->cmd_buf;
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

    simulator_write(&ps->sim, addr, val, ps->sim.ctask, FALSE);
}

/* Processes the continue command.
 * Returns TRUE on success.
 */
static
int palos_cmd_continue(struct palos *ps)
{
    ps->bps[0].enable = FALSE;
    if (unlikely(!palos_simulate(ps, -1, -1))) {
        report_error("palos: cmd_continue: could not simulate");
        return FALSE;
    }

    palos_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Processes the "next" command.
 * Returns TRUE on success.
 */
static
int palos_cmd_next(struct palos *ps)
{
    const char *arg, *end;
    int num;

    arg = (const char *) ps->cmd_buf;
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

    ps->bps[0].enable = FALSE;
    if (unlikely(!palos_simulate(ps, num, -1))) {
        report_error("palos: cmd_next: could not simulate");
        return FALSE;
    }

    palos_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Processes the "step" command.
 * Returns TRUE on success.
 */
static
int palos_cmd_step(struct palos *ps)
{
    const char *arg, *end;
    int num;

    arg = (const char *) ps->cmd_buf;
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

    ps->bps[0].enable = FALSE;
    if (unlikely(!palos_simulate(ps, -1, num))) {
        report_error("palos: cmd_step: could not simulate");
        return FALSE;
    }

    palos_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Processes the "next task" command.
 * Returns TRUE on success.
 */
int palos_cmd_next_task(struct palos *ps)
{
    struct breakpoint *bp;
    const char *arg, *end;
    uint8_t task;

    arg = (const char *) ps->cmd_buf;
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

    bp = &ps->bps[0];
    bp->enable = TRUE;
    bp->task = task;
    bp->ntask = 0xFF;
    bp->mpc = 0xFFFF;
    bp->on_task_switch = TRUE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;
    bp->allow_constants = TRUE;
    bp->addr = 0;
    bp->r_watch = FALSE;
    bp->watch = FALSE;

    if (unlikely(!palos_simulate(ps, -1, -1))) {
        report_error("palos: cmd_next_task: could not simulate");
        return FALSE;
    }

    palos_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Processes the "next nova" command.
 * Returns TRUE on success.
 */
int palos_cmd_next_nova(struct palos *ps)
{
    struct breakpoint *bp;
    const char *arg, *end;
    int num;

    arg = (const char *) ps->cmd_buf;
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

    bp = &ps->bps[0];
    bp->enable = TRUE;
    bp->task = TASK_EMULATOR;
    bp->ntask = 0xFF;
    bp->mpc = 020;
    bp->on_task_switch = FALSE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;
    bp->allow_constants = TRUE;
    bp->addr = 0;
    bp->r_watch = FALSE;
    bp->watch = FALSE;

    while (num-- > 0) {
        if (!gui_running(&ps->ui)) break;
        if (ps->sim.error) break;
        if (unlikely(!palos_simulate(ps, -1, -1))) {
            report_error("palos: cmd_next_nova: could not simulate");
            return FALSE;
        }
    }

    palos_cmd_nova_registers(ps);
    return TRUE;
}

/* Adds a breakpoint based on the string in the command buffer. */
static
void palos_cmd_add_breakpoint(struct palos *ps)
{
    const char *arg, *end;
    struct breakpoint *bp;
    uint32_t mask, val;
    unsigned int num;

    for (num = 1; num < ps->max_breakpoints; num++) {
        if (ps->bps[num].available) break;
    }
    if (num >= ps->max_breakpoints) {
        printf("maximum number of breakpoints reached\n");
        return;
    }

    bp = &ps->bps[num];
    bp->enable = FALSE;
    bp->task = 0xFF;
    bp->ntask = 0xFF;
    bp->mpc = 0xFFFF;
    bp->on_task_switch = FALSE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;
    bp->allow_constants = TRUE;
    bp->addr = 0;
    bp->r_watch = FALSE;
    bp->watch = FALSE;

    arg = (const char *) ps->cmd_buf;
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

        if (strcmp(arg, "-r_watch") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the register to watch\n");
                return;
            }
            bp->rsel = (uint16_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid rsel (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->r_watch = TRUE;
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
void palos_cmd_breakpoint_list(struct palos *ps)
{
    unsigned int num;
    struct breakpoint *bp;

    printf("NUM  EN  TASK  NTASK MPC     SW  MIR_FMT     "
           "MIR_MASK    CT  RSEL     ADDR\n");
    for (num = 1; num < ps->max_breakpoints; num++) {
        bp = &ps->bps[num];
        if (bp->available) continue;

        printf("%-4d %o   %03o   %03o   %06o  %o   "
               "%011o %011o %o   %06o%s  %06o%s\n",
               num, bp->enable ? 1 : 0,
               bp->task, bp->ntask, bp->mpc,
               bp->on_task_switch ? 1 : 0,
               bp->mir_fmt, bp->mir_mask,
               bp->allow_constants ? 1 : 0,
               bp->rsel, bp->r_watch ? "*" : " ",
               bp->addr, bp->watch ? "*" : " ");
    }
}

/* Enables or disables a breakpoint based on the parameter `enable`. */
static
void palos_cmd_breakpoint_enable(struct palos *ps, int enable)
{
    const char *arg, *end;
    struct breakpoint *bp;
    unsigned int num;

    arg = (const char *) ps->cmd_buf;
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

    if (num >= ps->max_breakpoints) {
        printf("breakpoint number exceeds maximum available\n");
        return;
    }

    bp = &ps->bps[num];
    bp->enable = enable;
    printf("breakpoint %u %s\n",
           num, (enable) ? "enabled" : "disabled");
}

/* Removes a breakpoint. */
static
void palos_cmd_breakpoint_remove(struct palos *ps)
{
    struct breakpoint *bp;
    const char *arg, *end;
    unsigned int num;

    arg = (const char *) ps->cmd_buf;
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

    if (num >= ps->max_breakpoints) {
        printf("breakpoint number exceeds maximum available\n");
        return;
    }

    bp = &ps->bps[num];
    if (bp->available) {
        printf("breakpoint %u is available\n", num);
    } else {
        ps->bps[num].available = TRUE;
        printf("breakpoint %u removed\n", num);
    }
}


/* Restarts the simulation. */
static
void palos_cmd_restart(struct palos *ps)
{
    simulator_reset(&ps->sim);
    palos_cmd_registers(ps, FALSE);
}


/* To run the debugger.
 * The parameter `ui` contains a reference to the gui object, which
 * in turn contains a reference to the palos object via `ui->arg`.
 * Returns TRUE on success.
 */
static
int palos_debug(struct gui *ui)
{
    struct palos *ps;
    unsigned int num;
    const char *cmd;

    ps = (struct palos *) ui->arg;

    for (num = 1; num < ps->max_breakpoints; num++) {
        ps->bps[num].available = TRUE;
    }
    ps->bps[0].available = FALSE;

    ps->cmd_buf[0] = '\0';
    ps->cmd_buf[1] = '\0';

    while (gui_running(ui)) {
        if (unlikely(!gui_update(ui))) {
            report_error("palos: debug: could not update GUI");
            return FALSE;
        }

        palos_get_command(ps);

        cmd = (const char *) ps->cmd_buf;

        if (strcmp(cmd, "r") == 0) {
            palos_cmd_registers(ps, FALSE);
            continue;
        }

        if (strcmp(cmd, "nr") == 0) {
            palos_cmd_nova_registers(ps);
            continue;
        }

        if (strcmp(cmd, "e") == 0) {
            palos_cmd_registers(ps, TRUE);
            continue;
        }

        if (strcmp(cmd, "dsk") == 0) {
            palos_cmd_disk_registers(ps);
            continue;
        }

        if (strcmp(cmd, "displ") == 0) {
            palos_cmd_display_registers(ps);
            continue;
        }

        if (strcmp(cmd, "ether") == 0) {
            palos_cmd_ethernet_registers(ps);
            continue;
        }

        if (strcmp(cmd, "d") == 0) {
            palos_cmd_dump_memory(ps);
            continue;
        }

        if (strcmp(cmd, "w") == 0) {
            palos_cmd_write_memory(ps);
            continue;
        }

        if (strcmp(cmd, "c") == 0) {
            if (unlikely(!palos_cmd_continue(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "n") == 0) {
            if (unlikely(!palos_cmd_next(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "s") == 0) {
            if (unlikely(!palos_cmd_step(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "nt") == 0) {
            if (unlikely(!palos_cmd_next_task(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "nn") == 0) {
            if (unlikely(!palos_cmd_next_nova(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "bp") == 0) {
            palos_cmd_add_breakpoint(ps);
            continue;
        }

        if (strcmp(cmd, "bl") == 0) {
            palos_cmd_breakpoint_list(ps);
            continue;
        }

        if (strcmp(cmd, "be") == 0) {
            palos_cmd_breakpoint_enable(ps, TRUE);
            continue;
        }

        if (strcmp(cmd, "bd") == 0) {
            palos_cmd_breakpoint_enable(ps, FALSE);
            continue;
        }

        if (strcmp(cmd, "br") == 0) {
            palos_cmd_breakpoint_remove(ps);
            continue;
        }

        if (strcmp(cmd, "zs") == 0) {
            palos_cmd_restart(ps);
            continue;
        }

        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("Commands:\n");
            printf("  r                Print the registers\n");
            printf("  nr               Print the NOVA registers\n");
            printf("  e                Print the extra registers\n");
            printf("  dsk              Print the disk registers\n");
            printf("  displ            Print the display registers\n");
            printf("  ether            Print the ethernet registers\n");
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
            printf("  -r_watch rsel    To watch for R register changes\n");
            printf("  -watch address   To watch for memory activity\n");
            continue;
        }

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            gui_stop(ui);
            break;
        }

        printf("invalid command\n");
        ps->cmd_buf[0] = '\0';
        ps->cmd_buf[1] = '\0';
    }

    return TRUE;
}

/* Print the program usage information. */
static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] microcode\n", prog_name);
    printf("where:\n");
    printf("  -c constant   Specify the constant rom file\n");
    printf("  -m micro      Specify the microcode rom file\n");
    printf("  -1 disk1      Specify the disk 1 filename\n");
    printf("  -2 disk2      Specify the disk 2 filename\n");
    printf("  -i            Set system type to Alto I\n");
    printf("  -ii_1krom     Set system type to Alto II (1K rom)\n");
    printf("  -ii_2krom     Set system type to Alto II (2K rom)\n");
    printf("  -ii_3kram     Set system type to Alto II (3K ram)\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{
    const char *const_filename;
    const char *mcode_filename;
    const char *disk1_filename;
    const char *disk2_filename;
    enum system_type sys_type;
    struct palos ps;
    int i, is_last;

    palos_initvar(&ps);
    const_filename = NULL;
    mcode_filename = NULL;
    disk1_filename = NULL;
    disk2_filename = NULL;
    sys_type = ALTO_II_3KRAM;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-c", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the constant rom file");
                return 1;
            }
            const_filename = argv[++i];
        } else if (strcmp("-m", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the microcode rom file");
                return 1;
            }
            mcode_filename = argv[++i];
        } else if (strcmp("-1", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the disk 1 file");
                return 1;
            }
            disk1_filename = argv[++i];
        } else if (strcmp("-2", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the disk 2 file");
                return 1;
            }
            disk2_filename = argv[++i];
        } else if (strcmp("-i", argv[i]) == 0) {
            sys_type = ALTO_I;
        } else if (strcmp("-ii_1krom", argv[i]) == 0) {
            sys_type = ALTO_II_1KROM;
        } else if (strcmp("-ii_2krom", argv[i]) == 0) {
            sys_type = ALTO_II_2KROM;
        } else if (strcmp("-ii_3kram", argv[i]) == 0) {
            sys_type = ALTO_II_3KRAM;
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            disk1_filename = argv[i];
        }
    }

    if (!mcode_filename) {
        report_error("main: must specify the microcode rom file name");
        return 1;
    }

    if (!const_filename) {
        report_error("main: must specify the constant rom file name");
        return 1;
    }

    if (unlikely(!palos_create(&ps, sys_type,
                               const_filename, mcode_filename,
                               disk1_filename, disk2_filename))) {
        report_error("main: could not create palos object");
        return 1;
    }

    if (unlikely(!palos_run(&ps))) {
        report_error("main: error while running");
        palos_destroy(&ps);
        return 1;
    }

    palos_destroy(&ps);
    return 0;
}
