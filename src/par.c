
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "common/utils.h"

/* Functions. */

/* Prints the usage information to the console output. */
static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] disk\n", prog_name);
    printf("where:\n");
    printf("  -2                Use double disk\n");
    printf("  -d dirname        Lists the contents of a directory\n");
    printf("  -e name filename  Extracts a given file\n");
    printf("  -i filename name  Inserts a given file\n");
    printf("  -v                Increase verbosity\n");
    printf("  --help            Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    const char *e_filename, *e_name;
    const char *i_filename, *i_name;
    const char *dirname;
    struct geometry dg;
    struct fs fs;
    struct file_entry dir_fe;
    int i, is_last, is_second_last;
    int found, modified;
    int verbose;

    disk_filename = NULL;
    e_filename = NULL;
    i_filename = NULL;
    e_name = NULL;
    i_name = NULL;
    dirname = NULL;
    modified = FALSE;
    verbose = 0;

    dg.num_disks = 1;
    dg.num_cylinders = 203;
    dg.num_heads = 2;
    dg.num_sectors = 12;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        is_second_last = (i + 2 >= argc);
        if (strcmp("-2", argv[i]) == 0) {
            dg.num_disks = 2;
        } else if (strcmp("-d", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the directory to list");
                return 1;
            }
            dirname = argv[++i];
        } else if (strcmp("-e", argv[i]) == 0) {
            if (is_second_last) {
                report_error("main: please specify the name to extract "
                             "and output filename");
                return 1;
            }
            e_name = argv[++i];
            e_filename = argv[++i];
        } else if (strcmp("-i", argv[i]) == 0) {
            if (is_second_last) {
                report_error("main: please specify the file to insert "
                             "and name in the filesystem");
                return 1;
            }
            i_filename = argv[++i];
            i_name = argv[++i];
        } else if (strcmp("-v", argv[i]) == 0) {
            verbose++;
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            disk_filename = argv[i];
        }
    }

    if (!disk_filename) {
        report_error("main: must specify the disk file name");
        return 1;
    }

    fs_initvar(&fs);
    if (unlikely(!fs_create(&fs, dg))) {
        report_error("main: could not create disk");
        goto error;
    }

    printf("loading disk image `%s`\n", disk_filename);
    if (!fs_load_image(&fs, disk_filename)) {
        report_error("main: could not load disk image");
        goto error;
    }

    if (!fs_check_integrity(&fs)) {
        report_error("main: invalid disk");
        goto error;
    }

    if (e_name != NULL) {
        if (!fs_extract_file(&fs, e_name, e_filename)) {
            report_error("main: could not extract `%s` to `%s`",
                         e_name, e_filename);
            goto error;
        }

        printf("extracted `%s` to `%s` successfully\n",
               e_name, e_filename);
    }

    if (i_name != NULL) {
        modified = TRUE;

        if (!fs_insert_file(&fs, i_filename, i_name)) {
            report_error("main: could not insert `%s` as `%s`",
                         i_filename, i_name);
            goto error;
        }

        printf("inserted `%s` as `%s` successfully\n",
               i_filename, i_name);
    }

    if (dirname) {
        if (!fs_resolve_name(&fs, dirname, &found, &dir_fe, NULL, NULL)) {
            report_error("main: could not resolve `%s`", dirname);
            goto error;
        }

        if (!found) {
            report_error("main: could not find `%s`", dirname);
            goto error;
        }

        if (!fs_print_directory(&fs, &dir_fe, verbose, stdout)) {
            report_error("main: could not print directory");
            return FALSE;
        }
    }

    if (modified) {
        if (!fs_save_image(&fs, disk_filename)) {
            report_error("main: could not save disk image");
            goto error;
        }
    }

    fs_destroy(&fs);
    return 0;

error:
    fs_destroy(&fs);
    return 1;
}
