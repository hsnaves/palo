
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
            report_error("fs: check_links: could not convert "
                         "virtual to real disk address: %u", vda);
            return FALSE;
        }

        if (pg->label.version == VERSION_FREE
            || pg->label.version == VERSION_BAD
            || pg->label.version == 0)
            continue;

        if (pg->label.prev_rda != 0) {
            if (!real_to_virtual(&fs->dg, pg->label.prev_rda, &ovda)) {
                report_error("fs: check_links: "
                             "invalid prev_rda = %u at VDA = %u",
                             pg->label.prev_rda, vda);
                success = FALSE;
                continue;
            }

            opg = &fs->pages[ovda];
            if (opg->label.file_pgnum != (pg->label.file_pgnum - 1)) {
                report_error("fs: check_links: "
                             "discontiguous file_pgnum (prev) "
                             "at VDA = %u: expecting %u but got %u",
                             vda, pg->label.file_pgnum - 1,
                             opg->label.file_pgnum);
                success = FALSE;
                continue;
            }

            if (opg->label.sn.word1 != pg->label.sn.word1
                || opg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_links: "
                             "differing file serial numbers (prev) at "
                             "VDA = %u: expecting %u, %u but got %u, %u",
                             vda, pg->label.sn.word1, pg->label.sn.word2,
                             opg->label.sn.word1, opg->label.sn.word2);
                success = FALSE;
                continue;
            }

            /* First page is special, so not test it. */
            if (opg->label.next_rda != rda && vda != 0) {
                report_error("fs: check_links: "
                             "broken link (prev) at VDA = %u: "
                             "points to RDA %u instead of %u",
                             vda, opg->label.next_rda, rda);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.file_pgnum != 0) {
                report_error("fs: check_links: "
                             "file_pgnum = %u is not zero at VDA = %u",
                             pg->label.file_pgnum, vda);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (!real_to_virtual(&fs->dg, pg->label.next_rda, &ovda)) {
                report_error("fs: check_links: "
                             "invalid next_rda = %u at VDA = %u",
                             pg->label.next_rda, vda);
                success = FALSE;
                continue;
            }

            opg = &fs->pages[ovda];
            if (opg->label.file_pgnum != (pg->label.file_pgnum + 1)) {
                report_error("fs: check_links: "
                             "discontiguous file_pgnum (next) "
                             "at VDA = %u: expecting %u but got %u",
                             vda, pg->label.file_pgnum + 1,
                             opg->label.file_pgnum);
                success = FALSE;
                continue;
            }

            if (opg->label.sn.word1 != pg->label.sn.word1
                || opg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_links: "
                             "differing file serial numbers (next) at "
                             "VDA = %u: expecting %u, %u but got %u, %u",
                             vda, pg->label.sn.word1, pg->label.sn.word2,
                             opg->label.sn.word1, opg->label.sn.word2);
                success = FALSE;
                continue;
            }

            /* First page is special, so not test it. */
            if (opg->label.prev_rda != rda && vda != 0) {
                report_error("fs: check_links: "
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
    if (fs->length <= 1) {
        report_error("fs: check_basic_data: filesystem too short");
        success = FALSE;
    }
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        virtual_to_real(&fs->dg, vda, &rda);
        if (pg->header[1] != rda || pg->header[0] != 0) {
            report_error("fs: check_basic_data: "
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

                report_error("fs: check_basic_data: "
                             "invalid bad page at VDA = %u: "
                             "expecting SN %u, %u, but got %u, %u",
                             vda, VERSION_BAD, VERSION_BAD,
                             pg->label.sn.word1, pg->label.sn.word2);
                success = FALSE;
            }
            continue;
        }

        if (pg->label.version == 0) {
            report_error("fs: check_basic_data: "
                         "invalid label version = 0 at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.nbytes > PAGE_DATA_SIZE) {
            report_error("fs: check_basic_data: "
                         "invalid label nbytes = %u at VDA = %u",
                         pg->label.nbytes, vda);
            success = FALSE;
            continue;
        }

        if (pg->label.prev_rda == 0) {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_basic_data: "
                             "short leader page at VDA = %u: "
                             "nbytes = %u", vda, pg->label.nbytes);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_basic_data: "
                             "short last page at VDA = %u: "
                             "nbytes = %u",
                             vda, pg->label.nbytes);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.nbytes >= PAGE_DATA_SIZE) {
                report_error("fs: check_basic_data: "
                             "full last page at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }
    }

    return success;
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
    struct file_info finfo;
    uint16_t idx, bit;

    tr = (struct traverse_files_result *) arg;

    /* Mark the existence of the file in the bitmap. */
    idx = IDX(fe->leader_vda);
    bit = BIT(fe->leader_vda);
    tr->fs->bitmap[idx] |= (1 << bit);

    file_info(fs, fe, &finfo);

    if (finfo.name_length >= NAME_LENGTH) {
        report_error("fs: check_files: "
                     "name too large");
        tr->error = TRUE;
    }
    if (fe->leader_vda == 1 && !finfo.has_dg) {
        report_error("fs: check_files: "
                     "missing disk geometry in SysDir properties");
        tr->error = TRUE;
    } else if (fe->leader_vda == 1 && fe->leader_vda == 1) {
        if (finfo.dg.num_disks != fs->dg.num_disks
            || finfo.dg.num_cylinders != fs->dg.num_cylinders
            || finfo.dg.num_heads != fs->dg.num_heads
            || finfo.dg.num_sectors != fs->dg.num_sectors) {

            report_error("fs: check_files: "
                         "invalid disk geometry");
            tr->error = TRUE;
        }
    }

    if (!check_file_properties(fs, fe)) {
        report_error("fs: check_files: "
                     "invalid file properties");
        tr->error = TRUE;
    }

    return TRUE;
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

    scan_files(fs, &traverse_files_cb, &tr);
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

    if (!check_directory_entry(fs, de)) {
        report_error("fs: check_sysdir: "
                     "directory entry %u at VDA %u",
                     tr->count, tr->dir_fe.leader_vda);
        tr->error = TRUE;
    }

    idx = IDX(de->fe.leader_vda);
    bit = BIT(de->fe.leader_vda);
    not_seen = (tr->fs->bitmap[idx] & (1 << bit));
    /* Mark this file as seen. */
    tr->fs->bitmap[idx] &= ~(1 << bit);

    /* TODO: Check for repeated entries in the directory. */

    /* Check if already went into this directory
     * to avoid infinite recursion.
     */
    if ((de->fe.sn.word1 & SN_DIRECTORY) && (not_seen)) {
        child_tr.fs = tr->fs;
        child_tr.dir_fe = de->fe;
        child_tr.count = 0;
        child_tr.error = FALSE;

        if (!check_directory_contents(fs, &de->fe)) {
            report_error("fs: check_sysdir: "
                         "invalid sub-directory: "
                         "directory entry %u at VDA %u",
                         tr->count, tr->dir_fe.leader_vda);
            tr->error = TRUE;
        }

        scan_directory(fs, &de->fe, &traverse_dirs_cb, &child_tr);

        if (child_tr.error) {
            /* Propagate errors up. */
            tr->error = TRUE;
        }
    }

    return TRUE;
}

/* Auxiliary function to fs_check_integrity().
 * Checks SysDir and its sub-directories.
 * Returns TRUE on success.
 */
static
int check_sysdir(struct fs *fs)
{
    struct file_entry sysdir_fe;
    struct directory_entry sysdir_de;
    struct traverse_dirs_result tr;
    unsigned int num_missing;
    uint16_t idx, bit;

    get_file_entry(fs, 1, &sysdir_fe);
    if (!check_file_entry(fs, &sysdir_fe)) {
        report_error("fs: check_sysdir: "
                     "no leader page at page 1");
        return FALSE;
    }

    /* Fake directory entry containing the SysDir. */
    sysdir_de.fe = sysdir_fe;
    sysdir_de.type = DIR_ENTRY_VALID;
    sysdir_de.length = (DIR_OFF_NAME / 2) + 4;
    sysdir_de.name_length = 6;
    strcpy(sysdir_de.name, "SysDir");

    tr.fs = fs;
    tr.dir_fe = sysdir_fe;
    tr.count = 0;
    tr.error = FALSE;

    traverse_dirs_cb(fs, &sysdir_de, &tr);
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
        report_error("fs: check_sysdir: "
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
    int found;

    resolve_name(fs, "DiskDescriptor", &found, &fe, NULL);
    if (!found) {
        report_error("fs: check_descriptor: "
                     "DiskDescriptor not found");
        return FALSE;
    }

    get_of(fs, &fe, TRUE, &of);

    nbytes = _read(fs, &of, buffer, sizeof(buffer));
    if (nbytes != sizeof(buffer)) {
        report_error("fs: check_descriptor: "
                     "could not read DiskDescriptor");
        return FALSE;
    }

    read_geometry(buffer, DESCR_OFF_GEOMETRY, &dg);
    diskbt_size = read_word_be(buffer, DESCR_OFF_DISKBT_SIZE);

    if (dg.num_disks != fs->dg.num_disks
        || dg.num_cylinders != fs->dg.num_cylinders
        || dg.num_heads != fs->dg.num_heads
        || dg.num_sectors != fs->dg.num_sectors) {

        report_error("fs: check_descriptor: "
                     "invalid disk geometry");
        return FALSE;
    }

    if (diskbt_size != fs->bitmap_size) {
        report_error("fs: check_descriptor: "
                     "invalid disk bitmap size");
        return FALSE;
    }

    return TRUE;
}

int fs_check_integrity(struct fs *fs)
{
    fs->checked = FALSE;

    if (!check_links(fs))
        return FALSE;

    if (!check_basic_data(fs))
        return FALSE;

    if (!check_files(fs))
        return FALSE;

    if (!check_sysdir(fs))
        return FALSE;

    if (!check_descriptor(fs))
        return FALSE;

    update_disk_metadata(fs);
    fs->checked = TRUE;
    return TRUE;
}

int check_file_entry(const struct fs *fs, const struct file_entry *fe)
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

int check_directory_entry(const struct fs *fs,
                          const struct directory_entry *de)
{
    uint16_t len;

    if (de->type == DIR_ENTRY_MISSING)
        return TRUE;

    if (!check_file_entry(fs, &de->fe)) {
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

int check_file_properties(const struct fs *fs,
                          const struct file_entry *fe)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    const uint8_t *data;
    uint8_t type, length;
    size_t i, nbytes;

    read_leader_page(fs, fe, buffer);
    if ((buffer[LD_OFF_PROPBEGIN] == 0)
        && (buffer[LD_OFF_PROPLEN] == 0)) return TRUE;

    if (2 * buffer[LD_OFF_PROPBEGIN] != LD_OFF_PROPS) {
        report_error("fs: check_file_properties: "
                     "PROPBEGIN = %u != %u",
                     2 * buffer[LD_OFF_PROPBEGIN], LD_OFF_PROPS);
        return FALSE;
    }

    nbytes = 2 * buffer[LD_OFF_PROPLEN];
    if (nbytes > (LD_OFF_SPARE - LD_OFF_PROPS)) {
        report_error("fs: check_file_properties: "
                     "invalid PROPLEN = %u",
                     buffer[LD_OFF_PROPLEN]);
        return FALSE;
    }

    data = &buffer[LD_OFF_PROPS];

    i = 0;
    while (i < nbytes) {
        type = data[i++];
        UNUSED(type);

        if (i == nbytes) {
            report_error("fs: check_file_properties: missing length");
            return FALSE;
        }
        length = data[i++];

        if (2 * length + i > nbytes) {
            report_error("fs: check_file_properties: overflow");
            return FALSE;
        }
        i += 2 * length;
    }

    return TRUE;
}

int check_directory_contents(const struct fs *fs,
                             const struct file_entry *dir_fe)
{
    struct open_file of;
    uint16_t w;
    uint8_t buffer[2];
    size_t to_read, nbytes;

    if (!check_file_entry(fs, dir_fe)) {
        report_error("fs: check_directory_contents: "
                     "file_entry does not match");
        return FALSE;
    }

    get_of(fs, dir_fe, TRUE, &of);

    while (TRUE) {
        nbytes = _read(fs, &of, buffer, 2);

        if (nbytes == 0) break;
        if (nbytes != 2) goto error_short;

        w = read_word_be(buffer, 0);
        to_read = 2 * ((size_t) (w & DIR_ENTRY_LEN_MASK));

        nbytes = _read(fs, &of, NULL, to_read - 2);
        if (nbytes != to_read - 2) goto error_short;
    }

    return TRUE;

error_short:
    report_error("fs: check_directory_contents: "
                 "entry too short");
    return FALSE;
}


int check_of(const struct fs *fs, struct open_file *of)
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
