
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "common/utils.h"

/* Data structures and types. */

/* Argument passed to print_dir_cb(). */
struct print_dir_cb_arg {
    unsigned int count;           /* Directory entry count. */
    int verbose;                  /* To print verbose information. */
};

/* Functions. */

/* Callback to print the files in the directory. */
static
int print_dir_cb(const struct fs *fs,
                 const struct directory_entry *de,
                 void *arg)
{
    struct file_info finfo;
    struct print_dir_cb_arg *cb_arg;
    uint32_t sn;
    struct tm *ltm;
    size_t length;

    cb_arg = ((struct print_dir_cb_arg *) arg);
    cb_arg->count++;

    if (!cb_arg->verbose && (cb_arg->count == 1)) {
        printf("N      VDA    SN     VER    SIZE        FILENAME\n");
    }

    if (de->type == DIR_ENTRY_MISSING) return TRUE;

    if (!fs_file_info(fs, &de->fe, &finfo)) {
        report_error("main: could not get file information of `%s`",
                     de->name);
        return FALSE;
    }

    if (!fs_file_length(fs, &de->fe, &length)) {
        report_error("main: could not get file length of `%s`",
                     de->name);
        return FALSE;
    }

    sn = ((uint32_t) (de->fe.sn.word1 & SN_PART1_MASK)) << 16;
    sn += de->fe.sn.word2;

    if (cb_arg->verbose) {
        printf("Leader VDA: %u\n", de->fe.leader_vda);
        printf("Serial number: %u\n", sn);
        printf("Version: %u\n", de->fe.version);
        printf("Name: %s\n", de->name);
        printf("Length: %u\n", (unsigned int) length);

        ltm = localtime(&finfo.created);
        printf("Created: %02d-%02d-%02d %2d:%02d:%02d\n",
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

        ltm = localtime(&finfo.written);
        printf("Written: %02d-%02d-%02d %2d:%02d:%02d\n",
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

        ltm = localtime(&finfo.read);
        printf("Read:    %02d-%02d-%02d %2d:%02d:%02d\n",
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

        printf("Propbegin: %u\n", finfo.propbegin);
        printf("Proplen: %u\n", finfo.proplen);
        if (finfo.has_dg) {
            printf("num_disks = %u, num_cylinders = %u\n"
                   "num_heads = %u, num_sectors = %u\n",
                   finfo.dg.num_disks, finfo.dg.num_cylinders,
                   finfo.dg.num_heads, finfo.dg.num_sectors);
        }
        printf("Consecutive: %u\n", finfo.consecutive);
        printf("Change SN: %u\n", finfo.change_sn);
        printf("Last page: \n");
        printf("  VDA: %u\n", finfo.last_page.vda);
        printf("  PGNUM: %u\n", finfo.last_page.pgnum);
        printf("  POS: %u\n", finfo.last_page.pos);
        printf("\n");
    } else {
        printf("%-6u %-6u %-6u %-6u %-10u  %-38s\n",
               cb_arg->count,  de->fe.leader_vda,
               sn, de->fe.version, (unsigned int) length,
               de->name);
    }

    return TRUE;
}

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
    struct print_dir_cb_arg cb_arg;
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
        if (!fs_resolve_name(&fs, dirname, &found, &dir_fe, NULL)) {
            report_error("main: could not resolve `%s`", dirname);
            goto error;
        }

        if (!found) {
            report_error("main: could not find `%s`", dirname);
            goto error;
        }

        cb_arg.count = 0;
        cb_arg.verbose = verbose;
        if (!fs_scan_directory(&fs, &dir_fe, &print_dir_cb, &cb_arg)) {
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
