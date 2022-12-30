
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by traverse_files_cb(). */
struct traverse_files_result {
    struct fs *fs;                /* A non-const reference. */
    int error;                    /* If found an error. */
};

/* Auxiliary data structure used by traverse_dirs_cb(). */
struct traverse_dirs_result {
    struct fs *fs;                /* A non-const reference. */
    struct file_entry dir_fe;     /* Current directory. */
    unsigned int count;           /* The entry count so far. */
    int error;                    /* If found an error. */
};

/* Auxiliary function to fs_check_integrity()
 * Checks that the pages are linked together correctly.
 * Returns TRUE on success.
 */
static
int check_links(const struct fs *fs)
{
    const struct page *pg, *opg;
    uint16_t vda, rda, ovda;
    int success;

    success = TRUE;
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        if (!virtual_to_real(&fs->dg, vda, &rda)) {
            report_error("fs: check_integrity: could not convert "
                         "virtual to real disk address: %u", vda);
            return FALSE;
        }

        if (pg->label.version == VERSION_FREE
            || pg->label.version == VERSION_BAD
            || pg->label.version == 0)
            continue;

        if (pg->label.prev_rda != 0) {
            if (!real_to_virtual(&fs->dg, pg->label.prev_rda, &ovda)) {
                report_error("fs: check_integrity: "
                             "invalid prev_rda = %u at VDA = %u",
                             pg->label.prev_rda, vda);
                success = FALSE;
                continue;
            }

            opg = &fs->pages[ovda];
            if (opg->label.file_pgnum != (pg->label.file_pgnum - 1)) {
                report_error("fs: check_integrity: "
                             "discontiguous file_pgnum (prev) "
                             "at VDA = %u: expecting %u but got %u",
                             vda, pg->label.file_pgnum - 1,
                             opg->label.file_pgnum);
                success = FALSE;
                continue;
            }

            if (opg->label.sn.word1 != pg->label.sn.word1
                || opg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_integrity: "
                             "differing file serial numbers (prev) at "
                             "VDA = %u: expecting %u, %u but got %u, %u",
                             vda, pg->label.sn.word1, pg->label.sn.word2,
                             opg->label.sn.word1, opg->label.sn.word2);
                success = FALSE;
                continue;
            }

            /* First page is special, so not test it. */
            if (opg->label.next_rda != rda && vda != 0) {
                report_error("fs: check_integrity: "
                             "broken link (prev) at VDA = %u: "
                             "points to RDA %u instead of %u",
                             vda, opg->label.next_rda, rda);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.file_pgnum != 0) {
                report_error("fs: check_integrity: "
                             "file_pgnum = %u is not zero at VDA = %u",
                             pg->label.file_pgnum, vda);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (!real_to_virtual(&fs->dg, pg->label.next_rda, &ovda)) {
                report_error("fs: check_integrity: "
                             "invalid next_rda = %u at VDA = %u",
                             pg->label.next_rda, vda);
                success = FALSE;
                continue;
            }

            opg = &fs->pages[ovda];
            if (opg->label.file_pgnum != (pg->label.file_pgnum + 1)) {
                report_error("fs: check_integrity: "
                             "discontiguous file_pgnum (next) "
                             "at VDA = %u: expecting %u but got %u",
                             vda, pg->label.file_pgnum + 1,
                             opg->label.file_pgnum);
                success = FALSE;
                continue;
            }

            if (opg->label.sn.word1 != pg->label.sn.word1
                || opg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_integrity: "
                             "differing file serial numbers (next) at "
                             "VDA = %u: expecting %u, %u but got %u, %u",
                             vda, pg->label.sn.word1, pg->label.sn.word2,
                             opg->label.sn.word1, opg->label.sn.word2);
                success = FALSE;
                continue;
            }

            /* First page is special, so not test it. */
            if (opg->label.prev_rda != rda && vda != 0) {
                report_error("fs: check_integrity: "
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

/* Auxiliary function to fs_check_integrity().
 * Checks some basic filesystem data.
 * Returns TRUE on success.
 */
static
int check_basic_data(const struct fs *fs)
{
    const struct page *pg;
    uint16_t vda, rda;
    int success;

    success = TRUE;
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        if (!virtual_to_real(&fs->dg, vda, &rda)) {
            report_error("fs: check_integrity: could not convert "
                         "virtual to real disk address: %u", vda);
            return FALSE;
        }

        if (pg->header[1] != rda || pg->header[0] != 0) {
            report_error("fs: check_integrity: "
                         "invalid page header at VDA = %u: "
                         "expecting %u, 0 but got %u, %u",
                         vda, rda, pg->header[1], pg->header[0]);
            success = FALSE;
            continue;
        }

        if (pg->label.version == VERSION_FREE) continue;
        if (pg->label.version == VERSION_BAD) {
            if (pg->label.sn.word1 != VERSION_BAD
                || pg->label.sn.word2 != VERSION_BAD) {

                report_error("fs: check_integrity: "
                             "invalid bad page at VDA = %u: "
                             "expecting SN %u, %u, but got %u, %u",
                             vda, VERSION_BAD, VERSION_BAD,
                             pg->label.sn.word1, pg->label.sn.word2);
                success = FALSE;
            }
            continue;
        }

        if (pg->label.version == 0) {
            report_error("fs: check_integrity: "
                         "invalid label version = 0 at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.nbytes > PAGE_DATA_SIZE) {
            report_error("fs: check_integrity: "
                         "invalid label nbytes = %u at VDA = %u",
                         pg->label.nbytes, vda);
            success = FALSE;
            continue;
        }

        if (pg->label.prev_rda == 0) {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "short leader page at VDA = %u: "
                             "nbytes = %u", vda, pg->label.nbytes);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "short last page at VDA = %u: "
                             "nbytes = %u",
                             vda, pg->label.nbytes);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.nbytes >= PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "full last page at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }
    }

    return success;
}

/* Auxiliary callback used by traverse_files_cb().
 * The `arg` parameter is a pointer to a traverse_files_result structure.
 */
static
int traverse_properties_cb(const struct fs *fs,
                           const struct file_entry *fe,
                           uint8_t type, uint8_t length,
                           const uint8_t *data, void *arg)
{
    struct traverse_files_result *tr;
    struct geometry dg;

    tr = (struct traverse_files_result *) arg;

    if (type == 1 && fe->leader_vda == 1) {
        if (length != 5) {
            report_error("fs: check_integrity: "
                         "invalid property length");
            tr->error = TRUE;
            return 1;
        }

        read_geometry(data, 0, &dg);
        if (dg.num_disks != fs->dg.num_disks
            || dg.num_cylinders != fs->dg.num_cylinders
            || dg.num_heads != fs->dg.num_heads
            || dg.num_sectors != fs->dg.num_sectors) {

            report_error("fs: check_integrity: "
                         "invalid disk geometry");
            tr->error = TRUE;
        }
    }
    return 1;
}

/* Auxiliary callback used by check_files().
 * The `arg` parameter is a pointer to a traverse_files_result structure.
 */
static
int traverse_files_cb(const struct fs *fs,
                      const struct file_entry *fe,
                      void *arg)
{
    struct traverse_files_result *tr;
    uint16_t idx, bit;

    tr = (struct traverse_files_result *) arg;

    /* Mark the existence of the file in the bitmap. */
    idx = IDX(fe->leader_vda);
    bit = BIT(fe->leader_vda);
    tr->fs->bitmap[idx] |= (1 << bit);

    if (!fs_scan_properties(fs, fe, &traverse_properties_cb, arg)) {
        report_error("fs: check_integrity: "
                     "could not scan properties");
        tr->error = TRUE;
    }

    return 1;
}

/* Auxiliary function to fs_check_integrity().
 * Checks all the files and their metadata.
 * Returns TRUE on success.
 */
static
int check_files(struct fs *fs)
{
    struct traverse_files_result tr;

    tr.fs = fs;
    tr.error = FALSE;
    memset(fs->bitmap, 0, fs->bitmap_size * sizeof(uint16_t));

    if (!fs_scan_files(fs, &traverse_files_cb, &tr)) {
        report_error("fs: check_integrity: "
                     "could not traverse files");
        return FALSE;
    }

    return (!tr.error);
}

/* Auxiliary callback used by check_sysdir().
 * The `arg` parameter is a pointer to a traverse_dirs_result structure.
 */
static
int traverse_dirs_cb(const struct fs *fs,
                     const struct directory_entry *de,
                     void *arg)
{
    struct traverse_dirs_result *tr;
    struct traverse_dirs_result child_tr;
    uint16_t idx, bit;
    int not_seen;

    tr = (struct traverse_dirs_result *) arg;
    tr->count++;

    if (de->type == DIR_ENTRY_MISSING)
        return 1;

    if (!fs_check_directory_entry(fs, de)) {
        report_error("fs: check_integrity: "
                     "directory entry %u at VDA %u",
                     tr->count, tr->dir_fe.leader_vda);
        tr->error = TRUE;
    }

    idx = IDX(de->fe.leader_vda);
    bit = BIT(de->fe.leader_vda);
    not_seen = (tr->fs->bitmap[idx] & (1 << bit));
    /* Mark this file as seen. */
    tr->fs->bitmap[idx] &= ~(1 << bit);

    if ((de->fe.sn.word1 & SN_DIRECTORY) && (not_seen)) {
        /* Check if already went into this directory
         * to avoid infinite recursion.
         */
        child_tr.fs = tr->fs;
        child_tr.dir_fe = de->fe;
        child_tr.count = 0;

        if (!fs_scan_directory(fs, &de->fe, &traverse_dirs_cb,
                               &child_tr)) {
            report_error("fs: check_integrity: "
                         "could not scan sub-directory: "
                         "directory entry %u at VDA %u",
                         tr->count, tr->dir_fe.leader_vda);
            tr->error = TRUE;
        }

        if (child_tr.error) {
            tr->error = TRUE;
        }
    }

    return 1;
}

/* Auxiliary function to fs_check_integrity().
 * Checks SysDir and its sub-directories.
 * Returns TRUE on success.
 */
static
int check_sysdir(struct fs *fs)
{
    struct file_entry root_fe;
    struct traverse_dirs_result tr;
    unsigned int num_missing;
    uint16_t idx, bit;

    if (!fs_file_entry(fs, 1, &root_fe)) {
        report_error("fs: check_integrity: "
                     "error finding SysDir at page 1");
        return FALSE;
    }

    idx = IDX(1);
    bit = BIT(1);
    if (!(fs->bitmap[idx] & (1 << bit))) {
        report_error("fs: check_integrity: "
                     "bitmap error at page 1");
        return FALSE;
    }
    fs->bitmap[idx] &= ~(1 << bit);

    tr.fs = fs;
    tr.dir_fe = root_fe;
    tr.count = 0;
    tr.error = FALSE;
    if (!fs_scan_directory(fs, &root_fe,
                           &traverse_dirs_cb, &tr)) {
        report_error("fs: check_integrity: "
                     "could not traverse SysDir");
        return FALSE;
    }
    if (tr.error) return FALSE;

    num_missing = 0;
    for (idx = 0; idx < fs->bitmap_size; idx++) {
        if (fs->bitmap[idx] == 0) continue;
        for (bit = 0; bit < 16; bit++) {
            if (fs->bitmap[idx] & (1 << bit)) {
                num_missing++;
            }
        }
    }

    if (num_missing > 0) {
        report_error("fs: check_integrity: "
                     "%u missing files", num_missing);
    }

    return TRUE;
}

/* Auxiliary function to fs_check_integrity().
 * Checks the disk geometry.
 * Returns TRUE on success.
 */
static
int check_descriptor(const struct fs *fs)
{
    struct file_entry fe;
    struct open_file of;
    struct geometry dg;
    uint8_t buffer[32];
    uint16_t diskbt_size;
    size_t nbytes;

    if (!fs_find_file(fs, "DiskDescriptor", &fe, NULL)) {
        report_error("fs: check_integrity: "
                     "could not find DiskDescriptor");
        return FALSE;
    }

    if (!fs_open(fs, &fe, &of)) {
        report_error("fs: check_integrity: "
                     "could not open DiskDescriptor");
        return FALSE;
    }
    /* Skip the leader page. */
    nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: check_integrity: error while reading");
        return FALSE;
    }


    nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
    if (nbytes != sizeof(buffer) || of.error) {
        report_error("fs: check_integrity: "
                     "could not read DiskDescriptor");
        return FALSE;
    }

    read_geometry(buffer, DESCR_OFF_GEOMETRY, &dg);
    diskbt_size = read_word_be(buffer, DESCR_OFF_DISKBT_SIZE);

    if (dg.num_disks != fs->dg.num_disks
        || dg.num_cylinders != fs->dg.num_cylinders
        || dg.num_heads != fs->dg.num_heads
        || dg.num_sectors != fs->dg.num_sectors) {

        report_error("fs: check_integrity: "
                     "invalid disk geometry");
        return FALSE;
    }

    if (diskbt_size != fs->bitmap_size) {
        report_error("fs: check_integrity: "
                     "invalid disk bitmap size");
        return FALSE;
    }

    return TRUE;
}

int fs_check_integrity(struct fs *fs, int level)
{
    if (level < 0) level = 5;

    if (level <= 0) goto check_okay;
    if (!check_links(fs))
        return FALSE;

    if (level <= 1) goto check_okay;
    if (!check_basic_data(fs))
        return FALSE;

    if (level <= 2) goto check_okay;
    if (!check_files(fs))
        return FALSE;

    if (level <= 3) goto check_okay;
    if (!check_sysdir(fs))
        return FALSE;

    if (level <= 4) goto check_okay;
    if (!check_descriptor(fs))
        return FALSE;

check_okay:
    fs_update_metadata(fs);
    return TRUE;
}

int fs_check_file_entry(const struct fs *fs, const struct file_entry *fe)
{
    const struct page *pg;

    if (fe->leader_vda >= fs->length) {
        report_error("fs: check_file_entry: "
                     "invalid VDA: %u", fe->leader_vda);
        return FALSE;
    }

    if (fe->version == VERSION_FREE) {
        report_error("fs: check_file_entry: "
                     "free page at VDA %u", fe->leader_vda);
        return FALSE;
    }

    if (fe->version == VERSION_BAD) {
        report_error("fs: check_file_entry: "
                     "bad page at VDA %u", fe->leader_vda);
        return FALSE;
    }

    if (fe->version == 0) {
        report_error("fs: check_file_entry: "
                     "invalid version at VDA %u", fe->leader_vda);
        return FALSE;
    }

    pg = &fs->pages[fe->leader_vda];

    if (pg->label.file_pgnum != 0) {
        report_error("fs: check_file_entry: "
                     "not the first page at VDA %u: %u",
                     fe->leader_vda, pg->label.file_pgnum);
        return FALSE;
    }

    if (fe->sn.word1 != pg->label.sn.word1
        || fe->sn.word2 != pg->label.sn.word2) {
        report_error("fs: check_file_entry: "
                     "SN does not match at VDA %u: "
                     "expecting %u, %u on disk, but got %u, %u",
                     fe->leader_vda, fe->sn.word1, fe->sn.word2,
                     pg->label.sn.word1, pg->label.sn.word2);
        return FALSE;
    }

    if (fe->version != pg->label.version) {
        report_error("fs: check_file_entry: "
                     "version does not match at VDA %u: "
                     "expecting %u on disk, but got %u",
                     fe->leader_vda, fe->version,
                     pg->label.version);
    }

    if (fe->blank != 0) {
        report_error("fs: check_file_entry: "
                     "blank = %u is not zero at VDA %u: ",
                     fe->blank, fe->leader_vda);
    }

    return TRUE;
}

int fs_check_directory_entry(const struct fs *fs,
                             const struct directory_entry *de)
{
    uint16_t len;

    if (de->type == DIR_ENTRY_MISSING)
        return TRUE;

    if (!fs_check_file_entry(fs, &de->fe)) {
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

int fs_check_of(const struct fs *fs, struct open_file *of)
{
    const struct page *pg;
    uint16_t vda;

    if (of->error) {
        report_error("fs: check_of: error on file");
        return FALSE;
    }

    /* Checks if reached the end of the file. */
    if (of->eof) return TRUE;

    vda = of->pos.vda;
    if (vda >= fs->length) {
        of->error = TRUE;
        report_error("fs: check_of: invalid VDA: %u", vda);
        return FALSE;
    }

    pg = &fs->pages[vda];
    if (of->pos.pos > pg->label.nbytes) {
        of->error = TRUE;
        report_error("fs: check_of: "
                     "inconsistent offset in page %u: "
                     "pos = %u, nbytes = %u",
                     vda, of->pos.pos, pg->label.nbytes);
        return FALSE;
    }

    return TRUE;
}
