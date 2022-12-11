
#include <stdio.h>
#include <string.h>

#include "simulator/simulator.h"
#include "common/utils.h"

static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] microcode\n", prog_name);
    printf("where:\n");
    printf("  -c constant   Specify the constant rom file\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{
    const char *constant_filename;
    const char *microcode_filename;
    struct simulator sim;
    int i, j, is_last;

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

    if (unlikely(!simulator_create(&sim))) {
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

    for (j = 0; j < 100; j++) {
        simulator_step(&sim);
    }

    simulator_destroy(&sim);
    return 0;

error:

    simulator_destroy(&sim);
    return 1;
}
