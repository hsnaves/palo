
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
    printf("  -f                To format the disk\n");
    printf("  -b name           To install the boot file\n");
    printf("  -s                Scavenges the filesystem\n");
    printf("  -d dir_name       Lists the contents of a directory\n");
    printf("  -e name filename  Extracts a given file\n");
    printf("  -i filename name  Inserts a given file\n");
    printf("  -c src dst        Copies from src to dst\n");
    printf("  -r name           Removes the link to name\n");
    printf("  -m dir_name       Creates a new directory\n");
    printf("  -nru              To not remove underlying files\n");
    printf("  -nud              To not update disk descriptor\n");
    printf("  -rw               Operate in read-write mode "
           "(default is read-only)\n");
    printf("  -v                Increase verbosity\n");
    printf("  --help            Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    const char *b_name;
    const char *e_filename, *e_name;
    const char *i_filename, *i_name;
    const char *c_src_name, *c_dst_name;
    const char *r_name;
    const char *m_dir_name;
    const char *dir_name;
    struct geometry dg;
    struct fs fs;
    int i, is_last, is_second_last;
    int should_format;
    int should_scavenge;
    int modified, not_read_only;
    int not_remove_underlying;
    int not_update_descriptor;
    int verbose, error;

    disk_filename = NULL;
    b_name = NULL;
    e_filename = NULL;
    i_filename = NULL;
    e_name = NULL;
    i_name = NULL;
    c_src_name = NULL;
    c_dst_name = NULL;
    r_name = NULL;
    m_dir_name = NULL;
    dir_name = NULL;
    should_format = FALSE;
    should_scavenge = FALSE;
    modified = FALSE;
    not_read_only = FALSE;
    not_remove_underlying = FALSE;
    not_update_descriptor = FALSE;
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
        } else if (strcmp("-f", argv[i]) == 0) {
            should_format = TRUE;
        } else if (strcmp("-b", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the name of the boot file");
                return 1;
            }
            b_name = argv[++i];
        } else if (strcmp("-s", argv[i]) == 0) {
            should_scavenge = TRUE;
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
        } else if (strcmp("-m", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the directory name");
                return 1;
            }
            m_dir_name = argv[++i];
        } else if (strcmp("-nru", argv[i]) == 0) {
            not_remove_underlying = TRUE;
        } else if (strcmp("-nud", argv[i]) == 0) {
            not_update_descriptor = TRUE;
        } else if (strcmp("-rw", argv[i]) == 0) {
            not_read_only = TRUE;
        } else if (strcmp("-v", argv[i]) == 0) {
            verbose++;
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

    if (should_format) {
        modified = TRUE;
        printf("formatting disk image\n");
        if (!fs_format(&fs, &error)) {
            report_error("main: could not format: %s",
                         fs_error(error));
            goto error;
        }
    } else {
        printf("loading disk image `%s`\n", disk_filename);
        if (!fs_load_image(&fs, disk_filename)) {
            report_error("main: could not load disk image");
            goto error;
        }
    }

    if (should_scavenge) {
        printf("scavenging the disk ...\n");
        fs_scavenge(&fs, stdout);
        printf("done scavenging\n");
    }

    if (!fs_check_integrity(&fs)) {
        report_error("main: invalid disk");
        goto error;
    }
    printf("filesystem checked: %u free pages\n", fs.free_pages);

    if (e_name != NULL) {
        if (!fs_extract_file(&fs, e_name, e_filename)) {
            report_error("main: could not extract `%s` to `%s`",
                         e_name, e_filename);
            goto error;
        }

        printf("extracted `%s` to `%s` successfully\n",
               e_name, e_filename);
    }

    if (b_name != NULL) {
        modified = TRUE;

        if (!fs_install_boot(&fs, b_name, &error)) {
            report_error("main: could not install boot file `%s`: %s",
                         b_name, fs_error(error));
            goto error;
        }

        printf("installed boot file `%s` successfully\n", b_name);
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

        printf("copied `%s` to `%s` successfully\n",
               c_src_name, c_dst_name);
    }

    if (r_name != NULL) {
        modified = TRUE;
        if (!fs_unlink(&fs, r_name, !not_remove_underlying, &error)) {
            report_error("main: could not unlink `%s`: %s",
                         r_name, fs_error(error));
            goto error;
        }
        printf("removed `%s` successfully\n",
               r_name);
    }

    if (m_dir_name != NULL) {
        modified = TRUE;
        if (!fs_mkdir(&fs, m_dir_name, &error)) {
            report_error("main: could not create directory `%s`: %s",
                         m_dir_name, fs_error(error));
            goto error;
        }
        printf("added directory `%s` successfully\n",
               m_dir_name);
    }

    if (dir_name) {
        if (!fs_print_directory(&fs, dir_name, verbose, stdout)) {
            report_error("main: could not print directory");
            return FALSE;
        }
    }

    if (modified && not_read_only) {
        printf("saving disk image `%s`\n", disk_filename);
        if (!not_update_descriptor) {
            if (!fs_update_disk_descriptor(&fs, &error)) {
                report_error("main: could not update disk descriptor: %s",
                             fs_error(error));
                goto error;
            }
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
