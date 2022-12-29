
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "common/utils.h"

/* Prints one property from the file. */
static
int print_property(const struct fs *fs,
                   const struct file_entry *fe,
                   uint8_t type, uint8_t length,
                   const uint8_t *data, void *arg)
{
    UNUSED(fs);
    UNUSED(fe);
    UNUSED(arg);

    if (type == 0) return 1;

    if (type == 1) {
        uint16_t num_disks, num_cylinders;
        uint16_t num_heads, num_sectors;

        num_disks = read_word_be(data, 0);
        num_cylinders = read_word_be(data, 2);
        num_heads = read_word_be(data, 4);
        num_sectors = read_word_be(data, 6);
        printf("num_disks = %u, num_cylinders = %u\n"
               "num_heads = %u, num_sectors = %u\n",
               num_disks, num_cylinders,
               num_heads, num_sectors);
    } else {
        printf("Property %u, length = %u\n", type, length);
    }
    return 1;
}

/* Prints the details of a file_info structure.
 * Returns TRUE on success.
 */
static
int print_file_info_details(const struct fs *fs,
                            const struct file_entry *fe,
                            const struct file_info *finfo)
{
    struct tm *ltm;

    ltm = localtime(&finfo->created);
    printf("Created: %02d-%02d-%02d %2d:%02d:%02d\n",
           ltm->tm_mday, ltm->tm_mon + 1,
           ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    ltm = localtime(&finfo->written);
    printf("Written: %02d-%02d-%02d %2d:%02d:%02d\n",
           ltm->tm_mday, ltm->tm_mon + 1,
           ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    ltm = localtime(&finfo->read);
    printf("Read:    %02d-%02d-%02d %2d:%02d:%02d\n",
           ltm->tm_mday, ltm->tm_mon + 1,
           ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

    printf("Propbegin: %u\n", finfo->propbegin);
    printf("Proplen: %u\n", finfo->proplen);
    if (!fs_scan_properties(fs, fe, &print_property, NULL)) {
        report_error("main: could not print properties");
        return FALSE;
    }

    printf("Consecutive: %u\n", finfo->consecutive);
    printf("Change SN: %u\n", finfo->change_sn);
    printf("File Entry: \n");
    printf("  VDA: %u\n", finfo->fe.leader_vda);
    printf("  SN: %u\n", finfo->fe.sn.word2);
    printf("  VER: %u\n", finfo->fe.version);
    printf("Last page: \n");
    printf("  VDA: %u\n", finfo->last_page.vda);
    printf("  PGNUM: %u\n", finfo->last_page.pgnum);
    printf("  POS: %u\n", finfo->last_page.pos);

    return TRUE;
}

/* Callback to print the files in the directory. */
static
int print_dir_cb(const struct fs *fs,
                 const struct directory_entry *de,
                 void *arg)
{
    struct file_info finfo;
    size_t length;
    int verbose;

    if (de->type == DIR_ENTRY_MISSING) return 1;

    verbose = *((int *) arg);
    if (!fs_file_info(fs, &de->fe, &finfo)) {
        report_error("main: could not get file information of `%s`",
                     de->name);
        return -1;
    }

    if (!fs_file_length(fs, &de->fe, &length, NULL)) {
        report_error("main: could not get file length of `%s`",
                     de->name);
        return -1;
    }

    if (verbose) {
        printf("Leader VDA: %u\n", de->fe.leader_vda);
        printf("Serial number: %u\n",
               ((de->fe.sn.word1 & SN_PART1_MASK) << 16) | de->fe.sn.word2);
        printf("Version: %u\n", de->fe.version);
        printf("Name: %s\n", de->name);
        printf("Length: %u\n", (unsigned int) length);
        if (verbose > 1) {
            if (!print_file_info_details(fs, &de->fe, &finfo)) {
                report_error("main: could not print file info detals `%s`",
                             de->name);
                return -1;
            }
        }
        printf("\n");
    } else {
        printf("%-6u %-6u %-6u %-9u  %-38s\n",
               de->fe.leader_vda, de->fe.sn.word2, de->fe.version,
               (unsigned int) length, de->name);
    }

    return 1;
}

/* Main function to print the files in the directory pointed by `fe`.
 * The verbosity level is indicated by `verbose`.
 * Returns TRUE on success.
 */
static
int print_directory(const struct fs *fs,
                    const struct file_entry *fe,
                    int verbose)
{
    if (!verbose)
        printf("VDA    SN     VER    SIZE       FILENAME\n");

    if (!fs_scan_directory(fs, fe, &print_dir_cb, &verbose)) {
        report_error("main: could not print directory");
        return FALSE;
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
    printf("  -2            Use double disk\n");
    printf("  -c level      To check the disk image\n");
    printf("  -d dirname    Lists the contents of a directory\n");
    printf("  -e filename   Extracts a given file\n");
    printf("  -v            Increase verbosity\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    const char *extract_filename;
    const char *dirname;
    char *end;
    struct geometry dg;
    struct fs fs;
    struct file_entry fe;
    int check_level;
    int i, is_last;
    int verbose;

    disk_filename = NULL;
    extract_filename = NULL;
    dirname = NULL;
    check_level = -1;
    verbose = 0;

    dg.num_disks = 1;
    dg.num_cylinders = 203;
    dg.num_heads = 2;
    dg.num_sectors = 12;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-2", argv[i]) == 0) {
            dg.num_disks = 2;
        } else if (strcmp("-c", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the check level");
                return 1;
            }
            check_level = strtol(argv[++i], &end, 10);
            if (end[0] != '\0') {
                report_error("main: invalid level: %s", argv[i]);
                return 1;
            }
        } else if (strcmp("-d", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the directory to list");
                return 1;
            }
            dirname = argv[++i];
        } else if (strcmp("-e", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the file to extract");
                return 1;
            }
            extract_filename = argv[++i];
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

    if (!fs_check_integrity(&fs, check_level)) {
        report_error("main: invalid disk");
        goto error;
    }

    if (extract_filename != NULL) {
        if (!fs_find_file(&fs, extract_filename, &fe, NULL)) {
            report_error("main: could not find %s", extract_filename);
            goto error;
        }

        if (!fs_extract_file(&fs, &fe, extract_filename, FALSE)) {
            report_error("main: could not extract %s", extract_filename);
            goto error;
        }

        printf("extracted `%s` successfully\n", extract_filename);
    }

    if (dirname) {
        if (!fs_find_file(&fs, dirname, &fe, NULL)) {
            report_error("main: could not find %s", dirname);
            goto error;
        }

        if (!print_directory(&fs, &fe, verbose)) goto error;
    }

    fs_destroy(&fs);
    return 0;

error:
    fs_destroy(&fs);
    return 1;
}
