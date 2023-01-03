
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "common/utils.h"

/* Data structures and types. */

/* Argument passed to print_dir_cb(). */
struct print_dir_cb_arg {
    FILE *fp;                     /* Output file. */
    unsigned int count;           /* Directory entry count. */
    int verbose;                  /* To print verbose information. */
};

/* Functions. */

/* Callback to print the files in the directory.
 * The argument `arg` is a pointer to print_dir_cb_arg structure.
 */
static
int print_dir_cb(const struct fs *fs,
                 const struct directory_entry *de,
                 void *arg)
{
    FILE *fp;
    struct file_info finfo;
    struct print_dir_cb_arg *cb_arg;
    uint32_t sn;
    struct tm *ltm;
    size_t length;
    int error;

    cb_arg = ((struct print_dir_cb_arg *) arg);
    cb_arg->count++;

    fp = cb_arg->fp;
    if (!cb_arg->verbose && (cb_arg->count == 1)) {
        fprintf(fp, "N      VDA    SN     VER    SIZE        FILENAME\n");
    }

    if (de->type != DIR_ENTRY_VALID)
        return TRUE;

    if (!fs_file_info(fs, &de->fe, &finfo, &error)) {
        report_error("fs: print_dir_cb: "
                     "could not get file information of `%s`: %s",
                     de->name, fs_error(error));
        return FALSE;
    }

    if (!fs_file_length(fs, &de->fe, &length, &error)) {
        report_error("fs: print_dir_cb: "
                     "could not get file length of `%s`: %s",
                     de->name, fs_error(error));
        return FALSE;
    }

    sn = ((uint32_t) (de->fe.sn.word1 & SN_PART1_MASK)) << 16;
    sn += de->fe.sn.word2;

    if (cb_arg->verbose) {
        fprintf(fp, "Leader VDA: %u\n", de->fe.leader_vda);
        fprintf(fp, "Serial number: %u\n", sn);
        fprintf(fp, "Version: %u\n", de->fe.version);
        fprintf(fp, "Name: %s\n", de->name);
        fprintf(fp, "Length: %u\n", (unsigned int) length);

        ltm = localtime(&finfo.created);
        fprintf(fp, "Created: %02d-%02d-%02d %2d:%02d:%02d\n",
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

        ltm = localtime(&finfo.written);
        fprintf(fp, "Written: %02d-%02d-%02d %2d:%02d:%02d\n",
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

        ltm = localtime(&finfo.read);
        fprintf(fp, "Read:    %02d-%02d-%02d %2d:%02d:%02d\n",
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

        fprintf(fp, "Propbegin: %u\n", finfo.propbegin);
        fprintf(fp, "Proplen: %u\n", finfo.proplen);
        if (finfo.has_dg) {
            fprintf(fp,
                    "num_disks = %u, num_cylinders = %u\n"
                    "num_heads = %u, num_sectors = %u\n",
                    finfo.dg.num_disks, finfo.dg.num_cylinders,
                    finfo.dg.num_heads, finfo.dg.num_sectors);
        }
        fprintf(fp, "Consecutive: %u\n", finfo.consecutive);
        fprintf(fp, "Change SN: %u\n", finfo.change_sn);
        fprintf(fp, "Last page: \n");
        fprintf(fp, "  VDA: %u\n", finfo.last_page.vda);
        fprintf(fp, "  PGNUM: %u\n", finfo.last_page.pgnum);
        fprintf(fp, "  POS: %u\n", finfo.last_page.pos);
        fprintf(fp, "\n");
    } else {
        fprintf(fp, "%-6u %-6u %-6u %-6u %-10u  %-38s\n",
                cb_arg->count,  de->fe.leader_vda,
                sn, de->fe.version, (unsigned int) length,
                de->name);
    }

    return TRUE;
}

int fs_print_directory(const struct fs *fs,
                       const char *dir_name,
                       int verbose, FILE *fp)
{
    struct print_dir_cb_arg cb_arg;
    struct file_entry dir_fe;
    int found, error;

    if (!fs_resolve_name(fs, dir_name, &found, &dir_fe, NULL, NULL)) {
        report_error("fs: print_directory: "
                     "could not resolve `%s`", dir_name);
        return FALSE;
    }

    if (!found) {
        report_error("fs: print_directory: "
                     "could not find `%s`", dir_name);
        return FALSE;
    }

    cb_arg.fp = fp;
    cb_arg.count = 0;
    cb_arg.verbose = verbose;
    if (!fs_scan_directory(fs, &dir_fe, &print_dir_cb, &cb_arg, &error)) {
        report_error("fs: print_directory: "
                     "could not scan directory `%s`: %s",
                     dir_name, fs_error(error));
        return FALSE;
    }
    return TRUE;
}
