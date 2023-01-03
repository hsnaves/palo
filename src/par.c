
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
    printf("  -d dir_name       Lists the contents of a directory\n");
    printf("  -e name filename  Extracts a given file\n");
    printf("  -i filename name  Inserts a given file\n");
    printf("  -c src dst        Copies from src to dst\n");
    printf("  -r name           Removes the link to name\n");
    printf("  -nru              To not remove underlying files\n");
    printf("  -ro               Operate in read only mode\n");
    printf("  -v                Increase verbosity\n");
    printf("  --help            Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    const char *dir_name;
    const char *e_filename, *e_name;
    const char *i_filename, *i_name;
    const char *c_src_name, *c_dst_name;
    const char *r_name;
    struct geometry dg;
    struct fs fs;
    int i, is_last, is_second_last;
    int modified, read_only;
    int not_remove_underlying;
    int verbose, error;

    disk_filename = NULL;
    dir_name = NULL;
    e_filename = NULL;
    i_filename = NULL;
    e_name = NULL;
    i_name = NULL;
    c_src_name = NULL;
    c_dst_name = NULL;
    r_name = NULL;
    modified = FALSE;
    read_only = FALSE;
    not_remove_underlying = FALSE;
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
            dir_name = argv[++i];
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
        } else if (strcmp("-c", argv[i]) == 0) {
            if (is_second_last) {
                report_error("main: please specify the src and dst");
                return 1;
            }
            c_src_name = argv[++i];
            c_dst_name = argv[++i];
        } else if (strcmp("-r", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the name to remove");
                return 1;
            }
            r_name = argv[++i];
        } else if (strcmp("-nru", argv[i]) == 0) {
            not_remove_underlying = TRUE;
        } else if (strcmp("-ro", argv[i]) == 0) {
            read_only = TRUE;
        } else if (strcmp("-v", argv[i]) == 0) {
            verbose = TRUE;
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            if (argv[i][0] == '-' && strlen(argv[i]) > 1) {
                report_error("main: invalid disk filename `%s`", argv[i]);
                return 1;
            }
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

    if (c_src_name != NULL && c_dst_name != NULL) {
        modified = TRUE;
        if (!fs_copy(&fs, c_src_name, c_dst_name)) {
            report_error("main: could not copy");
            goto error;
        }
    }

    if (r_name != NULL) {
        modified = TRUE;
        if (!fs_unlink(&fs, r_name, !not_remove_underlying, &error)) {
            report_error("main: could not unlink `%s`: %s",
                         r_name, fs_error(error));
            goto error;
        }
    }

    if (dir_name) {
        if (!fs_print_directory(&fs, dir_name, verbose, stdout)) {
            report_error("main: could not print directory");
            return FALSE;
        }
    }

    if (modified && !read_only) {
        if (!fs_update_disk_descriptor(&fs, &error)) {
            report_error("main: could not update disk descriptor: %s",
                         fs_error(error));
            goto error;
        }
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
