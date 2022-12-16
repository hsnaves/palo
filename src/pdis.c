
#include <stdio.h>
#include <string.h>

#include "disassembler/disassembler.h"
#include "microcode/microcode.h"
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
    struct disassembler dis;
    uint16_t address;
    uint8_t task;
    char buffer[512];
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

    disassembler_initvar(&dis);

    if (unlikely(!disassembler_create(&dis, ALTO_II_3KRAM))) {
        report_error("main: could not create disassembler");
        goto error;
    }

    if (unlikely(
            !disassembler_load_constant_rom(&dis, constant_filename))) {
        report_error("main: could not load constant rom");
        goto error;
    }

    if (unlikely(
            !disassembler_load_microcode_rom(&dis, microcode_filename))) {
        report_error("main: could not load microcode rom");
        goto error;
    }

    if (unlikely(!disassembler_find_task_addresses(&dis))) {
        report_error("main: could not find task addresses");
        goto error;
    }

    printf("ADDRESS TASK  MICROCODE    RSEL ALUF BS "
           "F1 F2 T L NEXT   STATEMENT\n");
    for (address = 0; address < 1024; address++) {
        uint16_t task_mask;

        task_mask = dis.insns[address].task_mask;
        for (task = 0; task < 16; task++) {
            if (task_mask == ((1 << task))) break;
            if (task_mask == (1 | ((1 << task)))) break;
        }
        if (task == 16) task = 0;
        disassembler_disassemble(&dis, address, task, buffer, sizeof(buffer));
        printf("%s\n", buffer);
    }

    disassembler_destroy(&dis);
    return 0;

error:
    disassembler_destroy(&dis);
    return 1;
}
