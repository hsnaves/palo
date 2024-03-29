
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "simulator/simulator.h"
#include "simulator/disk.h"
#include "simulator/ethernet.h"
#include "gui/gui.h"
#include "gui/udp_transport.h"
#include "debugger/debugger.h"
#include "common/utils.h"

/* Data structures and types. */

/* Internal structure for the palos simulator. */
struct palos {
    const char *const_filename;   /* The name of the constant rom. */
    const char *mcode_filename;   /* The name of the microcode rom. */
    const char *binary_filename;  /* The name of the binary code file. */
    const char *disk1_filename;   /* Disk 1 image file. */
    const char *disk2_filename;   /* Disk 2 image file. */

    struct gui ui;                /* The user input. */
    struct udp_transport utrp;    /* The UDP transport. */
    struct simulator sim;         /* The simulator. */
    struct debugger dbg;          /* The debugger. */
};

/* Functions. */

/* Initializes the palos variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
static
void palos_initvar(struct palos *ps)
{
    gui_initvar(&ps->ui);
    udp_transport_initvar(&ps->utrp);
    simulator_initvar(&ps->sim);
    debugger_initvar(&ps->dbg);
}

/* Destroys the palos object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
static
void palos_destroy(struct palos *ps)
{
    gui_destroy(&ps->ui);
    udp_transport_destroy(&ps->utrp);
    simulator_destroy(&ps->sim);
    debugger_destroy(&ps->dbg);
}

/* Creates a new palos object.
 * This obeys the initvar / destroy / create protocol.
 * The `sys_type` variable specifies the system type.
 * The `use_debugger` specifies whether or not to use the debugger.
 * The name of the several filenames to load related to the constant rom,
 * microcode rom, binary file, and disk images are given by the parameters:
 * `const_filename`, `mcode_filename`, `binary_filename`, `disk1_filename`,
 * and `disk2_filename`, respectively.
 * Returns TRUE on success.
 */
static
int palos_create(struct palos *ps,
                 enum system_type sys_type,
                 int use_debugger,
                 const char *const_filename,
                 const char *mcode_filename,
                 const char *binary_filename,
                 const char *disk1_filename,
                 const char *disk2_filename,
                 uint16_t address)
{
    palos_initvar(ps);

    if (unlikely(!simulator_create(&ps->sim, sys_type))) {
        report_error("palos: create: could not create simulator");
        palos_destroy(ps);
        return FALSE;
    }

    if (unlikely(!gui_create(&ps->ui, &ps->sim,
                             &debugger_debug, &ps->dbg))) {
        report_error("palos: create: could not create user interface");
        palos_destroy(ps);
        return FALSE;
    }

    if (unlikely(!udp_transport_create(&ps->utrp))) {
        report_error("palos: create: could not create UDP transport");
        palos_destroy(ps);
        return FALSE;
    }

    if (unlikely(!debugger_create(&ps->dbg, use_debugger,
                                  &ps->sim, &ps->ui))) {
        report_error("palos: create: could not create debugger");
        palos_destroy(ps);
        return FALSE;
    }

    ethernet_set_transport(&ps->sim.ether, &ps->utrp.trp);
    ethernet_set_address(&ps->sim.ether, address);

    ps->const_filename = const_filename;
    ps->mcode_filename = mcode_filename;
    ps->binary_filename = binary_filename;
    ps->disk1_filename = disk1_filename;
    ps->disk2_filename = disk2_filename;

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
    if (fn) {
        if (unlikely(!simulator_load_constant_rom(&ps->sim, fn))) {
            report_error("palos: run: could not load constant rom");
            return FALSE;
        }
    }

    fn = ps->mcode_filename;
    if (fn) {
        if (unlikely(!simulator_load_microcode_rom(&ps->sim, fn, 0))) {
            report_error("palos: run: could not load microcode rom");
            return FALSE;
        }
    }

    fn = ps->binary_filename;
    if (fn) {
        if (unlikely(!debugger_load_binary(&ps->dbg, fn, 0))) {
            report_error("palos: run: could not load binary file");
            return FALSE;
        }
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

/* Print the program usage information. */
static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] microcode\n", prog_name);
    printf("where:\n");
    printf("  -c constant   Specify the constant rom file\n");
    printf("  -m micro      Specify the microcode rom file\n");
    printf("  -b binary     Specify the binary code file\n");
    printf("  -1 disk1      Specify the disk 1 filename\n");
    printf("  -2 disk2      Specify the disk 2 filename\n");
    printf("  -i            Set system type to Alto I\n");
    printf("  -ii_1krom     Set system type to Alto II (1K rom)\n");
    printf("  -ii_2krom     Set system type to Alto II (2K rom)\n");
    printf("  -ii_3kram     Set system type to Alto II (3K ram)\n");
    printf("  -e addr       Set the ethernet address\n");
    printf("  -debug        To use the debugger\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{
    const char *const_filename;
    const char *mcode_filename;
    const char *binary_filename;
    const char *disk1_filename;
    const char *disk2_filename;
    enum system_type sys_type;
    struct palos ps;
    int i, is_last;
    uint16_t address;
    int use_debugger;

    palos_initvar(&ps);
    const_filename = NULL;
    mcode_filename = NULL;
    binary_filename = NULL;
    disk1_filename = NULL;
    disk2_filename = NULL;
    sys_type = ALTO_II_3KRAM;
    address = 100;
    use_debugger = FALSE;

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
        } else if (strcmp("-b", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the binary code file");
                return 1;
            }
            binary_filename = argv[++i];
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
        } else if (strcmp("-e", argv[i]) == 0) {
            char *endptr;
            if (is_last) {
                report_error("main: please specify the ethernet address");
                return 1;
            }
            address = strtoul(argv[++i], &endptr, 10);
            if (endptr[0] != '\0') {
                report_error("main: invalid address `%s`", argv[i]);
                return 1;
            }
        } else if (strcmp("-debug", argv[i]) == 0) {
            use_debugger = TRUE;
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            if (argv[i][0] == '-' && strlen(argv[i]) > 1) {
                report_error("main: invalid disk1 filename `%s`", argv[i]);
                return 1;
            }
            disk1_filename = argv[i];
        }
    }

    if (unlikely(!palos_create(&ps, sys_type, use_debugger,
                               const_filename, mcode_filename,
                               binary_filename, disk1_filename,
                               disk2_filename, address))) {
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
