
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by check_files_cb(). */
struct check_files_cb_arg {
    int has_error;                /* If found an error. */
};

/* Auxiliary data structure used by check_dirs_cb(). */
struct check_dirs_cb_arg {
    struct fs *fs;                /* A non-const reference. */
    struct file_entry dir_fe;     /* Current directory. */
    unsigned int count;           /* The entry count so far. */
    int has_error;                /* If found an error. */
};

/* Auxiliary data structure used by check_unique_cb(). */
struct check_unique_cb_arg {
    uint16_t num_files;           /* Total number of files. */
    uint16_t num_missing_files;   /* Total number of missing files. */
    unsigned int dir_count;       /* Directory count. */
    unsigned int max_dir_count;   /* The maximum directory count. */
    int print_missing;            /* To print the missing files. */
    struct serial_number *sns;    /* The serial numbers. */
    struct directory_entry *des;  /* The directory entries. */
    int has_error;                /* If found an error. */
};

/* Functions. */

/* Checks the directory_entry.
 * The directory_entry to check is in parameter `de`.
 * Returns TRUE if it is a valid directory_entry object.
 */
static
int check_directory_entry(const struct fs *fs,
                          const struct directory_entry *de)
{
    uint16_t len;

    if (de->type != DIR_ENTRY_VALID)
        return TRUE;

    if (!check_file_entry(fs, &de->fe, TRUE)) {
        report_error("fs: check_directory_entry: "
                     "file_entry does not match");
        return FALSE;
    }

    len = 2 * de->length;
    if (len <= DIR_OFF_NAME) {
        report_error("fs: check_directory_entry: "
                     "length of name (%u) is too short",
                     de->length);
        return FALSE;
    }

    if ((de->name_length + DIR_OFF_NAME) > len) {
        report_error("fs: check_directory_entry: "
                     "string buffer overflow: "
                     "name_length = %u, len = %u",
                     de->name_length, len);
        return FALSE;
    }

    return TRUE;
}

/* Checks the structure of the file properties is valid for a given file.
 * The parameter `fe` specifies the file.
 * Returns TRUE if the structure is valid.
 */
static
int check_prop_structure(const struct fs *fs,
                         const struct file_entry *fe)
{
    uint8_t buffer[MAX_PAGE_SIZE];
    const uint8_t *data;
    uint8_t type, length;
    size_t i, nbytes;

    read_leader_page(fs, fe, buffer);
    if ((buffer[LD_OFF_PROPBEGIN] == 0)
        && (buffer[LD_OFF_PROPLEN] == 0)) return TRUE;

    if (2 * buffer[LD_OFF_PROPBEGIN] != LD_OFF_PROPS) {
        report_error("fs: check_prop_structure: "
                     "PROPBEGIN = %u != %u at VDA %u",
                     2 * buffer[LD_OFF_PROPBEGIN], LD_OFF_PROPS,
                     fe->leader_vda);
        return FALSE;
    }

    nbytes = 2 * buffer[LD_OFF_PROPLEN];
    if (nbytes > (LD_OFF_SPARE - LD_OFF_PROPS)) {
        report_error("fs: check_prop_structure: "
                     "invalid PROPLEN = %u at VDA %u",
                     buffer[LD_OFF_PROPLEN], fe->leader_vda);
        return FALSE;
    }

    data = &buffer[LD_OFF_PROPS];

    i = 0;
    while (i < nbytes) {
        type = data[i++];
        UNUSED(type);

        if (i == nbytes) {
            report_error("fs: check_prop_structure: "
                         "missing length at VDA %u",
                         fe->leader_vda);
            return FALSE;
        }
        length = data[i++];

        if (2 * length + i > nbytes) {
            report_error("fs: check_prop_structure: "
                         "overflow at VDA %u",
                         fe->leader_vda);
            return FALSE;
        }
        i += 2 * length;
    }

    return TRUE;
}

/* Checks if the structure of the directory is valid.
 * The parameter `dir_fe` specifies the directory.
 * Returns TRUE if the directory structure is valid.
 */
static
int check_directory_structure(const struct fs *fs,
                              const struct file_entry *dir_fe)
{
    struct open_file of;
    struct directory_entry de;
    int ret;

    if (!check_file_entry(fs, dir_fe, TRUE)) {
        report_error("fs: check_directory_structure: "
                     "file_entry does not match");
        return FALSE;
    }

    fs_get_of(fs, dir_fe, TRUE, TRUE, &of);
    while (TRUE) {
        ret = fetch_directory_entry(fs, &of, &de);
        if (of.error < 0) {
            report_error("fs: check_directory_structure: "
                         "%s", fs_error(of.error));
            fs_close_ro(fs, &of);
            return FALSE;
        }
        if (!ret) break;
    }

    fs_close_ro(fs, &of);
    return TRUE;
}

/* Checks that the pages are linked together correctly.
 * Returns TRUE on success.
 */
static
int check_page_links(const struct fs *fs)
{
    const struct page *pg, *opg;
    uint16_t vda, rda, ovda;
    int success;

    success = TRUE;
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        if (!virtual_to_real(&fs->dg, vda, &rda)) {
            report_error("fs: check_page_links: "
                         "could not convert virtual to real "
                         "disk address: %u", vda);
            return FALSE;
        }

        if (pg->label.version == VERSION_FREE
            || pg->label.version == VERSION_BAD
            || pg->label.version == 0
            || vda == 0)
            continue;

        if (pg->label.prev_rda != 0) {
            if (!real_to_virtual(&fs->dg, pg->label.prev_rda, &ovda)) {
                report_error("fs: check_page_links: "
                             "invalid prev_rda = %u at VDA = %u",
                             pg->label.prev_rda, vda);
                success = FALSE;
                continue;
            }

            opg = &fs->pages[ovda];
            if (opg->label.file_pgnum != (pg->label.file_pgnum - 1)) {
                report_error("fs: check_page_links: "
                             "discontiguous file_pgnum (prev) "
                             "at VDA = %u: expecting %u but got %u",
                             vda, pg->label.file_pgnum - 1,
                             opg->label.file_pgnum);
                success = FALSE;
                continue;
            }

            if (opg->label.sn.word1 != pg->label.sn.word1
                || opg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_page_links: "
                             "differing file serial numbers (prev) at "
                             "VDA = %u: expecting %u, %u but got %u, %u",
                             vda, pg->label.sn.word1, pg->label.sn.word2,
                             opg->label.sn.word1, opg->label.sn.word2);
                success = FALSE;
                continue;
            }

            if (opg->label.next_rda != rda) {
                report_error("fs: check_page_links: "
                             "broken link (prev) at VDA = %u: "
                             "points to RDA %u instead of %u",
                             vda, opg->label.next_rda, rda);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.file_pgnum != 0) {
                report_error("fs: check_page_links: "
                             "file_pgnum = %u is not zero at VDA = %u",
                             pg->label.file_pgnum, vda);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (!real_to_virtual(&fs->dg, pg->label.next_rda, &ovda)) {
                report_error("fs: check_page_links: "
                             "invalid next_rda = %u at VDA = %u",
                             pg->label.next_rda, vda);
                success = FALSE;
                continue;
            }

            opg = &fs->pages[ovda];
            if (opg->label.file_pgnum != (pg->label.file_pgnum + 1)) {
                report_error("fs: check_page_links: "
                             "discontiguous file_pgnum (next) "
                             "at VDA = %u: expecting %u but got %u",
                             vda, pg->label.file_pgnum + 1,
                             opg->label.file_pgnum);
                success = FALSE;
                continue;
            }

            if (opg->label.sn.word1 != pg->label.sn.word1
                || opg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_page_links: "
                             "differing file serial numbers (next) at "
                             "VDA = %u: expecting %u, %u but got %u, %u",
                             vda, pg->label.sn.word1, pg->label.sn.word2,
                             opg->label.sn.word1, opg->label.sn.word2);
                success = FALSE;
                continue;
            }

            if (opg->label.prev_rda != rda) {
                report_error("fs: check_page_links: "
                             "broken link (next) at VDA = %u: "
                             "points to RDA %u instead of %u",
                             vda, opg->label.prev_rda, rda);
                success = FALSE;
                continue;
            }
        }
    }

    return success;
}

/* Checks some basic filesystem data.
 * Returns TRUE on success.
 */
static
int check_basic_filesystem_data(const struct fs *fs)
{
    const struct page *pg;
    uint16_t vda, rda;
    int success;

    success = TRUE;
    if (fs->length <= 1) {
        report_error("fs: check_basic_filesystem_data: "
                     "filesystem too short");
        success = FALSE;
    }
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        virtual_to_real(&fs->dg, vda, &rda);
        if (pg->header[1] != rda || pg->header[0] != 0) {
            report_error("fs: check_basic_filesystem_data: "
                         "invalid page header at VDA = %u: "
                         "expecting %u, 0 but got %u, %u",
                         vda, rda, pg->header[1], pg->header[0]);
            success = FALSE;
            continue;
        }

        if (pg->label.unused != 0) {
            report_error("fs: check_basic_filesystem_data: "
                         "invalid unused at VDA = %u: %u",
                         vda, pg->label.unused);
            success = FALSE;
            continue;
        }

        if (pg->label.version == VERSION_BAD) continue;
        if (pg->label.version == VERSION_FREE) {
            if (pg->label.sn.word1 != VERSION_FREE
                || pg->label.sn.word2 != VERSION_FREE) {

                report_error("fs: check_basic_filesystem_data: "
                             "invalid free page at VDA = %u: "
                             "expecting SN %u, %u, but got %u, %u",
                             vda, VERSION_FREE, VERSION_FREE,
                             pg->label.sn.word1, pg->label.sn.word2);
                success = FALSE;
            }
            continue;
        }

        if (pg->label.version == 0) {
            report_error("fs: check_basic_filesystem_data: "
                         "invalid label version = 0 at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.nbytes > fs->sector_bytes) {
            report_error("fs: check_basic_filesystem_data: "
                         "invalid label nbytes = %u at VDA = %u",
                         pg->label.nbytes, vda);
            success = FALSE;
            continue;
        }

        if (vda == 0) continue;

        if (pg->label.prev_rda == 0) {
            if (pg->label.nbytes < fs->sector_bytes) {
                report_error("fs: check_basic_filesystem_data: "
                             "short leader page at VDA = %u: "
                             "nbytes = %u", vda, pg->label.nbytes);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (pg->label.nbytes < fs->sector_bytes) {
                report_error("fs: check_basic_filesystem_data: "
                             "short last page at VDA = %u: "
                             "nbytes = %u",
                             vda, pg->label.nbytes);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.nbytes >= fs->sector_bytes) {
                report_error("fs: check_basic_filesystem_data: "
                             "full last page at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }
    }

    return success;
}

/* Auxiliary callback used by check_files().
 * The `arg` parameter is a pointer to a check_files_cb_arg structure.
 */
static
int check_files_cb(const struct fs *fs,
                   const struct file_entry *fe,
                   void *arg)
{
    struct check_files_cb_arg *cb_arg;
    struct file_info finfo;
    int error;

    cb_arg = (struct check_files_cb_arg *) arg;

    fs_get_file_info(fs, fe, &finfo, &error);
    if (error < 0) {
        report_error("fs: check_files: "
                     "could not get file information: %s",
                     fs_error(error));
        cb_arg->has_error = TRUE;
    }
    if (finfo.name_length >= NAME_LENGTH) {
        report_error("fs: check_files: name too large");
        cb_arg->has_error = TRUE;
    }
    if (fe->leader_vda == 1 && !finfo.has_dg) {
        report_error("fs: check_files: "
                     "missing disk geometry in SysDir properties");
        cb_arg->has_error = TRUE;
    } else if (fe->leader_vda == 1 && fe->leader_vda == 1) {
        if (finfo.dg.num_disks != fs->dg.num_disks
            || finfo.dg.num_cylinders != fs->dg.num_cylinders
            || finfo.dg.num_heads != fs->dg.num_heads
            || finfo.dg.num_sectors != fs->dg.num_sectors) {

            report_error("fs: check_files: "
                         "invalid disk geometry");
            cb_arg->has_error = TRUE;
        }
    }

    if (!check_prop_structure(fs, fe)) {
        report_error("fs: check_files: "
                     "invalid file properties");
        cb_arg->has_error = TRUE;
    }

    return TRUE;
}

/* Checks all the files and their metadata.
 * Returns TRUE on success.
 */
static
int check_files(struct fs *fs)
{
    struct check_files_cb_arg cb_arg;

    cb_arg.has_error = FALSE;
    scan_files(fs, &check_files_cb, &cb_arg);
    return (!cb_arg.has_error);
}

/* Auxiliary callback used by check_dirs().
 * The `arg` parameter is a pointer to a check_dirs_cb_arg structure.
 */
static
int check_dirs_cb(const struct fs *fs,
                  const struct directory_entry *de,
                  void *arg)
{
    struct check_dirs_cb_arg *cb_arg;
    struct check_dirs_cb_arg child_arg;
    int not_seen;

    cb_arg = (struct check_dirs_cb_arg *) arg;
    cb_arg->count++;

    if (de->type != DIR_ENTRY_VALID)
        return TRUE;

    if (!check_directory_entry(fs, de)) {
        report_error("fs: check_dirs: "
                     "directory entry %u at VDA %u",
                     cb_arg->count, cb_arg->dir_fe.leader_vda);
        cb_arg->has_error = TRUE;
    }

    not_seen = (cb_arg->fs->ref_count[de->fe.leader_vda]++ == 0);

    /* Check if already went into this directory
     * to avoid infinite recursion.
     */
    if ((de->fe.sn.word1 & SN_DIRECTORY) && (not_seen)) {
        child_arg.fs = cb_arg->fs;
        child_arg.dir_fe = de->fe;
        child_arg.count = 0;
        child_arg.has_error = FALSE;

        if (!check_directory_structure(fs, &de->fe)) {
            report_error("fs: check_dirs: "
                         "invalid sub-directory: "
                         "directory entry %u at VDA %u",
                         cb_arg->count, cb_arg->dir_fe.leader_vda);
            cb_arg->has_error = TRUE;
        }

        scan_directory(fs, &de->fe, &check_dirs_cb, &child_arg);

        if (child_arg.has_error) {
            /* Propagate errors up. */
            cb_arg->has_error = TRUE;
        }
    }

    return TRUE;
}

/* Checks SysDir and its sub-directories (recursively).
 * Returns TRUE on success.
 */
static
int check_dirs(struct fs *fs)
{
    struct file_entry sysdir_fe;
    struct directory_entry sysdir_de;
    struct check_dirs_cb_arg cb_arg;

    fs_get_sysdir(fs, &sysdir_fe);
    if (!check_file_entry(fs, &sysdir_fe, TRUE)) {
        report_error("fs: check_dirs: "
                     "no leader page at page 1");
        return FALSE;
    }

    /* Fake directory entry containing the SysDir. */
    sysdir_de.fe = sysdir_fe;
    sysdir_de.type = DIR_ENTRY_VALID;
    memset(sysdir_de.name, 0, sizeof(sysdir_de.name));
    strcpy(sysdir_de.name, "SysDir.");
    sysdir_de.name_length = strlen(sysdir_de.name);
    update_directory_entry_length(&sysdir_de);

    cb_arg.fs = fs;
    cb_arg.dir_fe = sysdir_fe;
    cb_arg.count = 0;
    cb_arg.has_error = FALSE;

    memset(fs->ref_count, 0, fs->length * sizeof(uint16_t));
    check_dirs_cb(fs, &sysdir_de, &cb_arg);
    if (cb_arg.has_error) return FALSE;

    return TRUE;
}

/* Auxiliary callback used by check_unique_cb().
 * The `arg` parameter is a pointer to a check_unique_cb_arg structure.
 */
static
int check_unique_dir_cb(const struct fs *fs,
                        const struct directory_entry *de,
                        void *arg)
{
    struct check_unique_cb_arg *cb_arg;

    UNUSED(fs);
    cb_arg = (struct check_unique_cb_arg *) arg;
    if (de->type != DIR_ENTRY_VALID)
        return TRUE;

    if (cb_arg->des) {
        cb_arg->des[cb_arg->dir_count] = *de;
    }
    cb_arg->dir_count++;
    return TRUE;
}

/* Auxiliary function used by qsort() to sort an array
 * of directory_entry objects.
 */
static
int cmp_directory_entry(const void *p1, const void *p2)
{
    const struct directory_entry *de1;
    const struct directory_entry *de2;

    de1 = (const struct directory_entry *) p1;
    de2 = (const struct directory_entry *) p2;

    if (de1->type != DIR_ENTRY_VALID) return -1;
    if (de2->type != DIR_ENTRY_VALID) return 1;

    return directory_entry_compare(de1, de2->name, de2->name_length);
}

/* Auxiliary callback used by check_unique().
 * The `arg` parameter is a pointer to a check_unique_cb_arg structure.
 */
static
int check_unique_cb(const struct fs *fs,
                    const struct file_entry *fe,
                    void *arg)
{
    struct check_unique_cb_arg *cb_arg;
    struct serial_number *sn;
    uint16_t idx;

    cb_arg = (struct check_unique_cb_arg *) arg;
    if (cb_arg->sns) {
        sn = &cb_arg->sns[cb_arg->num_files];
        sn->word1 = fe->sn.word1 & SN_PART1_MASK;
        sn->word2 = fe->sn.word2;
    }
    cb_arg->num_files++;

    if (fs->ref_count[fe->leader_vda] == 0) {
        if (cb_arg->print_missing) {
            report_error("fs: check_unique: "
                         "missing file at VDA %u",
                         fe->leader_vda);
        }
        cb_arg->num_missing_files++;
    }

    if (!(fe->sn.word1 & SN_DIRECTORY))
        return TRUE;

    cb_arg->dir_count = 0;
    scan_directory(fs, fe, &check_unique_dir_cb, arg);
    if (cb_arg->max_dir_count < cb_arg->dir_count) {
        cb_arg->max_dir_count = cb_arg->dir_count;
    }

    if (!cb_arg->des || (cb_arg->dir_count <= 1))
        return TRUE;

    qsort(cb_arg->des,
          cb_arg->dir_count,
          sizeof(struct directory_entry),
          &cmp_directory_entry);

    for (idx = 0; idx < cb_arg->dir_count - 1; idx++) {
        if (cmp_directory_entry(&cb_arg->des[idx],
                                &cb_arg->des[idx + 1]) == 0) {

            report_error("fs: check_unique: "
                         "repeated directory_entry in LDA: %u",
                         fe->leader_vda);
            cb_arg->has_error = TRUE;
            break;
        }
    }

    return TRUE;
}

/* Auxiliary function used by qsort() to sort an array
 * of serial_number objects.
 */
static
int cmp_serial_number(const void *p1, const void *p2)
{
    const struct serial_number *sn1;
    const struct serial_number *sn2;

    sn1 = (const struct serial_number *) p1;
    sn2 = (const struct serial_number *) p2;

    if (sn1->word1 < sn2->word1) return -1;
    if (sn1->word1 > sn2->word1) return 1;
    if (sn1->word2 < sn2->word2) return -1;
    if (sn1->word2 > sn2->word2) return 1;

    return 0;
}

/* Checks for unique serial numbers and unique names in directories.
 * Returns TRUE on success.
 */
static
int check_unique(struct fs *fs)
{
    struct check_unique_cb_arg cb_arg;
    uint16_t idx;
    size_t size;

    cb_arg.num_files = 0;
    cb_arg.num_missing_files = 0;
    cb_arg.dir_count = 0;
    cb_arg.max_dir_count = 0;
    cb_arg.print_missing = TRUE;
    cb_arg.sns = NULL;
    cb_arg.des = NULL;
    cb_arg.has_error = FALSE;
    scan_files(fs, &check_unique_cb, &cb_arg);

    if (cb_arg.num_missing_files) {
        report_error("fs: check_unique: %u missing files",
                     cb_arg.num_missing_files);
    }

    size = cb_arg.num_files * sizeof(struct serial_number);
    cb_arg.sns = (struct serial_number *) malloc(size);

    size = cb_arg.max_dir_count * sizeof(struct directory_entry);
    cb_arg.des = (struct directory_entry *) malloc(size);

    if (unlikely(!cb_arg.sns || !cb_arg.des)) {
        if (cb_arg.sns) free((void *) cb_arg.sns);
        if (cb_arg.des) free((void *) cb_arg.des);

        report_error("fs: check_unique: memory exhausted");
        return FALSE;
    }

    cb_arg.num_files = 0;
    cb_arg.num_missing_files = 0;
    cb_arg.dir_count = 0;
    cb_arg.max_dir_count = 0;
    cb_arg.print_missing = FALSE;
    cb_arg.has_error = FALSE;
    scan_files(fs, &check_unique_cb, &cb_arg);

    if (cb_arg.num_files > 1) {
        qsort(cb_arg.sns,
              cb_arg.num_files,
              sizeof(struct serial_number),
              &cmp_serial_number);

        for (idx = 0; idx < cb_arg.num_files - 1; idx++) {
            if (cmp_serial_number(&cb_arg.sns[idx],
                                  &cb_arg.sns[idx + 1]) == 0) {
                report_error("fs: check_unique: "
                             "repeated serial_number: %u, %u",
                             cb_arg.sns[idx].word1,
                             cb_arg.sns[idx].word2);
                cb_arg.has_error = TRUE;
            }
        }
    }

    free((void *) cb_arg.sns);
    free((void *) cb_arg.des);
    return !cb_arg.has_error;
}

/* Checks the DiskDescriptor file.
 * Returns TRUE on success.
 */
static
int check_disk_descriptor(const struct fs *fs)
{
    struct open_file of;
    struct geometry dg;
    uint8_t buffer[32];
    uint16_t diskbt_size;
    size_t nbytes;

    if (!fs_open_ro(fs, "DiskDescriptor.", &of)) {
        report_error("fs: check_disk_descriptor: "
                     "DiskDescriptor not found");
        goto error_check;
    }

    nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
    if (nbytes != sizeof(buffer)) {
        report_error("fs: check_disk_descriptor: "
                     "could not read DiskDescriptor");
        goto error_check;
    }

    read_geometry(buffer, DESCR_OFF_GEOMETRY, &dg);
    diskbt_size = read_word_be(buffer, DESCR_OFF_DISKBT_SIZE);

    if (dg.num_disks != fs->dg.num_disks
        || dg.num_cylinders != fs->dg.num_cylinders
        || dg.num_heads != fs->dg.num_heads
        || dg.num_sectors != fs->dg.num_sectors) {

        report_error("fs: check_disk_descriptor: "
                     "invalid disk geometry");
        goto error_check;
    }

    if (diskbt_size != fs->bitmap_size) {
        report_error("fs: check_disk_descriptor: "
                     "invalid disk bitmap size");
        goto error_check;
    }

error_check:
    fs_close_ro(fs, &of);
    return TRUE;
}

int fs_check_integrity(struct fs *fs)
{
    fs->checked = TRUE;

    if (!check_page_links(fs))
        goto check_error;

    if (!check_basic_filesystem_data(fs))
        goto check_error;

    if (!check_files(fs))
        goto check_error;

    if (!check_dirs(fs))
        goto check_error;

    if (!check_unique(fs))
        goto check_error;

    if (!check_disk_descriptor(fs))
        goto check_error;

    update_disk_metadata(fs);
    update_reference_counts(fs);
    return TRUE;

check_error:
    fs->checked = FALSE;
    return FALSE;
}

/* Auxiliary function for fs_scavenge().
 * The `arg` points to a file where to print the status information.
 */
static
int scavenge_cb(const struct fs *fs,
                const struct file_entry *fe,
                void *arg)
{
    struct open_file of;
    struct file_info finfo;
    uint8_t buffer[MAX_PAGE_SIZE];
    size_t ret, nbytes;
    FILE *fp, *out;

    out = (FILE *) arg;

    fprintf(out, "file at %u: ", fe->leader_vda);

    if (!fs_get_file_info(fs, fe, &finfo, &of.error)) {
        fprintf(out, "error\n");
        report_error("fs: scavenge: "
                     "could not get file information: %s",
                     fs_error(of.error));
        return TRUE;
    }

    fprintf(out, "%s\n", finfo.name);
    if (!fs_get_of(fs, fe, TRUE, TRUE, &of)) {
        report_error("fs: scavenge: "
                     "could not open file: %s",
                     fs_error(of.error));
        return TRUE;
    }

    fp = fopen(finfo.name, "wb");
    if (!fp) {
        report_error("fs: scavenge: "
                     "could not open `%s` for writing",
                     finfo.name);
        fs_close_ro(fs, &of);
        return TRUE;
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
        if (of.error < 0) {
            report_error("fs: scavenge: "
                         "error while reading: %s",
                         fs_error(of.error));
            break;
        }

        if (nbytes > 0) {
            ret = fwrite(buffer, 1, nbytes, fp);
            if (ret != nbytes) {
                report_error("fs: scavenge: "
                             "error while writing `%s`",
                             finfo.name);
                break;
            }
        }

        if (nbytes < sizeof(buffer)) break;
    }

    fclose(fp);
    fs_close_ro(fs, &of);
    return TRUE;
}

void fs_scavenge(const struct fs *fs, FILE *fp)
{
    struct fs *fs_rw;
    int checked;

    checked = fs->checked;

    /* Pretend it is checked. */
    fs_rw = (struct fs *) fs;
    fs_rw->checked = TRUE;
    scan_files(fs, &scavenge_cb, fp);
    fs_rw->checked = checked;
}

int check_file_entry(const struct fs *fs,
                     const struct file_entry *fe,
                     int verbose)
{
    const struct page *pg;

    if (!fs->checked) {
        return FALSE;
    }

    if (fe->leader_vda >= fs->length) {
        if (verbose) {
            report_error("fs: check_file_entry: "
                         "invalid leader VDA: %u", fe->leader_vda);
        }
        return FALSE;
    }

    if (fe->version == VERSION_FREE || fe->version == 0
        || fe->version == VERSION_BAD) {

        if (verbose) {
            report_error("fs: check_file_entry: "
                         "invalid version at VDA %u: %u",
                         fe->leader_vda, fe->version);
        }
        return FALSE;
    }

    pg = &fs->pages[fe->leader_vda];
    if (pg->label.file_pgnum != 0) {
        if (verbose) {
            report_error("fs: check_file_entry: "
                         "file_pgnum = %u != 0 at VDA %u",
                         pg->label.file_pgnum, fe->leader_vda);
        }
        return FALSE;
    }

    if (fe->sn.word1 != pg->label.sn.word1
        || fe->sn.word2 != pg->label.sn.word2) {
        if (verbose) {
            report_error("fs: check_file_entry: "
                         "serial number %u, %u does not match %u, %u "
                         "at VDA %u",
                         fe->sn.word1, fe->sn.word2,
                         pg->label.sn.word1, pg->label.sn.word2,
                         fe->leader_vda);
        }
        return FALSE;
    }

    if (fe->version != pg->label.version) {
        if (verbose) {
            report_error("fs: check_file_entry: "
                         "version %u does not match %u at VDA %u",
                         fe->version, pg->label.version,
                         fe->leader_vda);
        }
        return FALSE;
    }

    return TRUE;
}

int check_of(const struct fs *fs, struct open_file *of)
{
    const struct page *pg;
    uint16_t vda;

    if (of->error < 0) return FALSE;

    if (!fs->checked) {
        of->error = ERROR_FS_UNCHECKED;
        return FALSE;
    }

    /* Checks if reached the end of the file. */
    if (of->eof) return TRUE;

    vda = of->pos.vda;
    if (vda >= fs->length) {
        of->error = ERROR_INVALID_OF;
        return FALSE;
    }

    pg = &fs->pages[vda];
    if (of->pos.pos > pg->label.nbytes) {
        of->error = ERROR_INVALID_OF;
        return FALSE;
    }

    return TRUE;
}
