
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "simulator/simulator.h"
#include "common/utils.h"

/* Print the program usage information. */
static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] microcode\n", prog_name);
    printf("where:\n");
    printf("  -c constant   Specify the constant rom file\n");
    printf("  --help        Print this help\n");
}

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
void debug_simulation(struct simulator *sim)
{
    char cmd_buffer[256];
    char out_buffer[4096];
    const char *cmd, *arg, *end;
    unsigned int num;
    uint16_t addr, val;
    int running;

    cmd_buffer[0] = '\0';
    cmd_buffer[1] = '\0';

    running = TRUE;
    while (running) {
        get_command(cmd_buffer, sizeof(cmd_buffer));

        cmd = (const char *) cmd_buffer;
        arg = &cmd[strlen(cmd) + 1];

        if (strcmp(cmd, "n") == 0) {
            if (arg[0] != '\0') {
                num = strtoul(arg, (char **) &end, 10);
                if (end[0] != '\0') {
                    printf("invalid number %s\n", arg);
                    continue;
                }
            } else {
                num = 1;
            }

            while (num-- > 0)
                simulator_step(sim);

            goto disassemble;
        }

        if (strcmp(cmd, "r") == 0) goto disassemble;

        if (strcmp(cmd, "d") == 0) {
            if (arg[0] != '\0') {
                addr = (uint16_t) strtoul(arg, (char **) &end, 8);
                if (end[0] != '\0') {
                    printf("invalid octal number %s\n", arg);
                    continue;
                }
            } else {
                addr = 0;
            }

            num = 8;
            while (num-- > 0) {
                val = simulator_read(sim, addr, sim->ctask, FALSE);
                printf("%06o: %06o\n", addr++, val);
            }
            continue;
        }

        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("Commands:\n");
            printf("  n [num]     Step through the microcode\n");
            printf("  r           Print the registers\n");
            printf("  d [addr]    Dump the memory contents\n");
            printf("  h           Print this help\n");
            printf("  q           Quit the debugger\n");
            continue;
        }

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0)
            break;

        continue;

    disassemble:
        simulator_disassemble(sim, out_buffer, sizeof(out_buffer));
        printf("%s\n", out_buffer);
        simulator_print_registers(sim, out_buffer, sizeof(out_buffer));
        printf("%s\n", out_buffer);
        continue;
    }
}

int main(int argc, char **argv)
{
    const char *constant_filename;
    const char *microcode_filename;
    struct simulator sim;
    int i, is_last;

    constant_filename = NULL;
    microcode_filename = NULL;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-c", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the constant rom file");
                return 1;
            }
            constant_filename = argv[++i];
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            microcode_filename = argv[i];
        }
    }

    if (!microcode_filename) {
        report_error("main: must specify the microcode rom file name");
        return 1;
    }

    if (!constant_filename) {
        report_error("main: must specify the constant rom file name");
        return 1;
    }

    simulator_initvar(&sim);

    if (unlikely(!simulator_create(&sim, ALTO_II_3KRAM))) {
        report_error("main: could not create simulator");
        goto error;
    }

    if (unlikely(!simulator_load_constant_rom(&sim, constant_filename))) {
        report_error("main: could not load constant rom");
        goto error;
    }

    if (unlikely(
            !simulator_load_microcode_rom(&sim, microcode_filename, 0))) {
        report_error("main: could not load microcode rom");
        goto error;
    }

    simulator_reset(&sim);
    debug_simulation(&sim);

    simulator_destroy(&sim);
    return 0;

error:

    simulator_destroy(&sim);
    return 1;
}
