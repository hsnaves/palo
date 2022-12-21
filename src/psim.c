
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "simulator/simulator.h"
#include "simulator/disk.h"
#include "simulator/display.h"
#include "simulator/ethernet.h"
#include "gui/gui.h"
#include "common/utils.h"

/* Data structures and types. */

/* Internal structure for the user interface. */
struct psim_internal {
    const char *const_filename;   /* The name of the constant rom. */
    const char *mcode_filename;   /* The name of the microcode rom. */
    const char *disk1_filename;   /* Disk 1 image file. */
    const char *disk2_filename;   /* Disk 2 image file. */
};

/* Functions. */

/* Gets a command line from the standard input.
 * The command line is stored in `buffer`, with the words separated by a
 * NUL character. The last word is ended with two consecutive NUL characters.
 * The size of the buffer is given by `buffer_size`.
 */
static
void get_command(char *buffer, size_t buffer_size)
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

        if (i + 2 < buffer_size)
            buffer[i++] = (char) c;
        len++;
    }

    /* Return the same command as before. */
    if (i == 0) return;

    if (!last_is_space) {
        buffer[i++] = '\0';
        len++;
    }
    buffer[i++] = '\0';
    len++;

    if (len >= buffer_size) {
        printf("command too long\n");
        buffer[0] = '\0';
        buffer[1] = '\0';
    }
}

/* To run the debugger. */
static
int debug_simulation(struct gui *ui)
{
    char cmd_buffer[256];
    char out_buffer[4096];
    struct simulator *sim;
    struct string_buffer output;
    const char *cmd, *arg, *end;
    unsigned int num;
    uint16_t addr, val;
    int should_disassemble;

    sim = ui->sim;

    cmd_buffer[0] = '\0';
    cmd_buffer[1] = '\0';

    output.buf = out_buffer;
    output.buf_size = sizeof(out_buffer);

    while (gui_running(ui)) {
        get_command(cmd_buffer, sizeof(cmd_buffer));

        cmd = (const char *) cmd_buffer;
        arg = &cmd[strlen(cmd) + 1];
        should_disassemble = 0;

        if (strcmp(cmd, "n") == 0) {
            if (arg[0] != '\0') {
                num = strtoul(arg, (char **) &end, 10);
                if (end[0] != '\0') {
                    printf("invalid number %s\n", arg);
                    goto next_command;
                }
            } else {
                num = 1;
            }

            while ((num-- > 0) && gui_running(ui))
                simulator_step(sim);

            should_disassemble = 1;
            goto next_command;
        }

        if (strcmp(cmd, "nt") == 0) {
            if (arg[0] != '\0') {
                num = strtoul(arg, (char **) &end, 10);
                if (end[0] != '\0') {
                    printf("invalid number %s\n", arg);
                    goto next_command;
                }
            } else {
                num = TASK_NUM_TASKS;
            }

            while (gui_running(ui)) {
                do {
                    simulator_step(sim);
                } while ((sim->ctask == sim->ntask)
                         && gui_running(ui));

                if (num == sim->ntask) break;
                if (num >= TASK_NUM_TASKS) break;
            }

            should_disassemble = 1;
            goto next_command;
        }

        if (strcmp(cmd, "r") == 0) {
            should_disassemble = 1;
            goto next_command;
        }

        if (strcmp(cmd, "e") == 0) {
            should_disassemble = 2;
            goto next_command;
        }

        if (strcmp(cmd, "dsk") == 0) {
            string_buffer_reset(&output);
            disk_print_registers(&sim->dsk, &output);
            printf("%s\n", out_buffer);
            goto next_command;
        }

        if (strcmp(cmd, "displ") == 0) {
            string_buffer_reset(&output);
            display_print_registers(&sim->displ, &output);
            printf("%s\n", out_buffer);
            goto next_command;
        }

        if (strcmp(cmd, "ether") == 0) {
            string_buffer_reset(&output);
            ethernet_print_registers(&sim->ether, &output);
            printf("%s\n", out_buffer);
            goto next_command;
        }

        if (strcmp(cmd, "d") == 0) {
            if (arg[0] != '\0') {
                addr = (uint16_t) strtoul(arg, (char **) &end, 8);
                if (end[0] != '\0') {
                    printf("invalid octal number %s\n", arg);
                    goto next_command;
                }
            } else {
                addr = 0;
            }

            num = 8;
            while ((num-- > 0) && gui_running(ui)) {
                val = simulator_read(sim, addr, sim->ctask, FALSE);
                printf("%06o: %06o\n", addr++, val);
            }
            goto next_command;
        }

        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("Commands:\n");
            printf("  n [num]     Step through the microcode\n");
            printf("  nt [task]   Step until switch task\n");
            printf("  r           Print the registers\n");
            printf("  e           Print the extra registers\n");
            printf("  d [addr]    Dump the memory contents\n");
            printf("  dsk         Print the disk registers\n");
            printf("  displ       Print the display registers\n");
            printf("  ether       Print the ethernet registers\n");
            printf("  h           Print this help\n");
            printf("  q           Quit the debugger\n");
            goto next_command;
        }

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            gui_stop(ui);
            break;
        }


    next_command:
        if (!gui_running(ui)) break;

        if (unlikely(!gui_update(ui))) {
            report_error("debug_simulation: could not update GUI");
            return FALSE;
        }

        if (should_disassemble) {
            string_buffer_reset(&output);
            simulator_disassemble(sim, &output);
            printf("%s\n", out_buffer);

            string_buffer_reset(&output);
            if (should_disassemble == 1) {
                simulator_print_registers(sim, &output);
            } else {
                simulator_print_extra_registers(sim, &output);
            }
            printf("%s\n", out_buffer);
        }
    }

    return TRUE;
}

/* Runs the palo simulator.
 * Returns TRUE on success.
 */
static
int run_psim(struct gui *ui, struct simulator *sim,
             struct psim_internal *pi)
{
    if (unlikely(!simulator_create(sim, ALTO_II_3KRAM))) {
        report_error("run_psim: could not create simulator");
        return FALSE;
    }

    if (unlikely(!simulator_load_constant_rom(sim,
                                              pi->const_filename))) {
        report_error("run_psim: could not load constant rom");
        return FALSE;
    }

    if (unlikely(!simulator_load_microcode_rom(sim,
                                               pi->mcode_filename, 0))) {
        report_error("run_psim: could not load microcode rom");
        return FALSE;
    }

    if (pi->disk1_filename) {
        if (unlikely(!disk_load_image(&sim->dsk, 0,
                                      pi->disk1_filename))) {
            report_error("run_psim: could not load disk 1");
            return FALSE;
        }
    }

    if (pi->disk2_filename) {
        if (unlikely(!disk_load_image(&sim->dsk, 1,
                                      pi->disk2_filename))) {
            report_error("run_psim: could not load disk 2");
            return FALSE;
        }
    }

    simulator_reset(sim);

    if (unlikely(!gui_create(ui, sim, &debug_simulation, pi))) {
        report_error("run_psim: could not create user interface");
        return FALSE;
    }

    if (unlikely(!gui_start(ui))) {
        report_error("run_psim: could not start user interface");
        return FALSE;
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
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{
    struct psim_internal pi;
    struct simulator sim;
    struct gui ui;
    int i, is_last, ret;

    simulator_initvar(&sim);
    gui_initvar(&ui);
    pi.const_filename = NULL;
    pi.mcode_filename = NULL;
    pi.disk1_filename = NULL;
    pi.disk2_filename = NULL;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-c", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the constant rom file");
                return 1;
            }
            pi.const_filename = argv[++i];
        } else if (strcmp("-m", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the microcode rom file");
                return 1;
            }
            pi.mcode_filename = argv[++i];
        } else if (strcmp("-1", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the disk 1 file");
                return 1;
            }
            pi.disk1_filename = argv[++i];
        } else if (strcmp("-2", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the disk 2 file");
                return 1;
            }
            pi.disk2_filename = argv[++i];
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            pi.disk1_filename = argv[i];
        }
    }

    if (!pi.mcode_filename) {
        report_error("main: must specify the microcode rom file name");
        return 1;
    }

    if (!pi.const_filename) {
        report_error("main: must specify the constant rom file name");
        return 1;
    }

    ret = run_psim(&ui, &sim, &pi);
    gui_destroy(&ui);
    simulator_destroy(&sim);
    return (ret) ? 0 : 1;
}
