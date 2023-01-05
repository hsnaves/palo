
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assembler/assembler.h"
#include "parser/parser.h"
#include "common/utils.h"

static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] input\n", prog_name);
    printf("where:\n");
    printf("  -l listing    Specify the output listing file\n");
    printf("  -c constant   Specify the constant rom file\n");
    printf("  -m microcode  Specify the microcode rom file\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{
    const char *input_filename;
    const char *listing_filename;
    const char *constant_filename;
    const char *microcode_filename;
    const char *fn;
    struct assembler as;
    int i, is_last;

    input_filename = NULL;
    listing_filename = NULL;
    constant_filename = NULL;
    microcode_filename = NULL;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-l", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the listing file");
                return 1;
            }
            listing_filename = argv[++i];
        } else if (strcmp("-c", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the constant rom file");
                return 1;
            }
            constant_filename = argv[++i];
        } else if (strcmp("-m", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the microcode rom file");
                return 1;
            }
            microcode_filename = argv[++i];
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            if (argv[i][0] == '-' && strlen(argv[i]) > 1) {
                report_error("main: invalid input filename `%s`", argv[i]);
                return 1;
            }
            input_filename = argv[i];
        }
    }

    if (!input_filename) {
        report_error("main: must specify the input file name");
        return 1;
    }

    assembler_initvar(&as);

    if (unlikely(!assembler_create(&as))) {
        report_error("main: could not create assembler");
        goto error;
    }

    fn = input_filename;
    if (unlikely(parser_parse(&as.p, fn) == ERROR)) {
        report_error("main: could not parse file");
        goto error;
    }

    if (as.p.num_errors > 0) {
        parser_report_errors(&as.p);
        goto error;
    }

    if (unlikely(!assembler_resolve_constants(&as))) {
        report_error("main: could not resolve constants");
        goto error;
    }

    if (unlikely(!assembler_resolve_labels(&as))) {
        report_error("main: could not resolve labels");
        goto error;
    }

    if (unlikely(!assembler_assemble(&as))) {
        report_error("main: could not assemble");
        goto error;
    }

    fn = constant_filename;
    if (fn) {
        if (unlikely(!assembler_dump_constant_rom(&as, fn))) {
            report_error("main: could not write constant rom");
            goto error;
        }
    }

    fn = microcode_filename;
    if (fn) {
        if (unlikely(!assembler_dump_microcode_rom(&as, fn))) {
            report_error("main: could not write microcode rom");
            goto error;
        }
    }

    fn = listing_filename;
    if (fn) {
        if (unlikely(!assembler_print_listing(&as, fn))) {
            report_error("main: could not write listing file");
            goto error;
        }
    }

    assembler_destroy(&as);
    return 0;

error:
    assembler_destroy(&as);
    return 1;
}
