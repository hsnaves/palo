
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
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

/* Auxiliary data structure used by fs_find_file(). */
struct find_result {
    const char *name;             /* The name of the searched file. */
    size_t name_length;           /* Length of the name. */
    struct file_entry fe;         /* The file_entry of the file. */
    int found;                    /* If the file was found. */
};

/* Constants. */

/* Offsets within the page. */
#define PAGE_HEADER offsetof(struct page, header)
#define PAGE_LABEL   offsetof(struct page, label)
#define PAGE_DATA     offsetof(struct page, data)

/* Offsets within the leader page data. */
#define LEADER_CREATED                    0U
#define LEADER_WRITTEN                    4U
#define LEADER_READ                       8U
#define LEADER_NAME                      12U
#define LEADER_PROPS                     52U
#define LEADER_SPARE                    472U
#define LEADER_PROPBEGIN                492U
#define LEADER_PROPLEN                  493U
#define LEADER_CONSECUTIVE              494U
#define LEADER_CHANGESN                 495U
#define LEADER_DIRFPHINT                496U
#define LEADER_LASTPAGEHINT             506U

/* Offsets within the directory entry. */
#define DIRECTORY_SN                      2U
#define DIRECTORY_VERSION                 6U
#define DIRECTORY_LEADER_VDA             10U
#define DIRECTORY_NAME                   12U

/* Other constants. */
#define DIR_ENTRY_TYPE_SHIFT              10
#define DIR_ENTRY_LEN_MASK            0x3FFU

/* Offsets in the DiscDescriptor file. */
#define DESCRIPTOR_NUM_DISKS              0U
#define DESCRIPTOR_NUM_CYLINDERS          2U
#define DESCRIPTOR_NUM_HEADS              4U
#define DESCRIPTOR_NUM_SECTORS            6U
#define DESCRIPTOR_LAST_SN                8U
#define DESCRIPTOR_BLANK                 12U
#define DESCRIPTOR_DISKBT_SIZE           14U
#define DESCRIPTOR_VERSIONS_KEPT         16U
#define DESCRIPTOR_FREE_PAGES            18U

#define IDX(vda) ((vda) >> 4)
#define BIT(vda) (15 - ((vda) & 15))
#define VDA(idx, bit) (((idx) << 4) + (15 - (bit)))

/* Forward declarations. */
static void copy_name(char *dst, const char *src);

/* Functions. */

void fs_initvar(struct fs *fs)
{
    fs->pages = NULL;
    fs->bitmap = NULL;
}

void fs_destroy(struct fs *fs)
{
    if (fs->pages) free((void *) fs->pages);
    fs->pages = NULL;

    if (fs->bitmap) free((void *) fs->bitmap);
    fs->bitmap = NULL;
}

int fs_create(struct fs *fs, struct geometry dg)
{
    size_t size;
    fs_initvar(fs);

    if (unlikely(dg.num_disks > 2
                 || dg.num_heads > 2
                 || dg.num_sectors > 15
                 || dg.num_cylinders >= 512)) {
        report_error("fs: create: invalid disk geometry");
        return FALSE;
    }

    fs->dg = dg;

    fs->length = dg.num_disks * dg.num_cylinders
        * dg.num_heads * dg.num_sectors;
    fs->bitmap_size = (fs->length >> 4);
    if (fs->length & 0xF) fs->bitmap_size++;

    size = ((size_t) fs->length) * sizeof(struct page);
    fs->pages = (struct page *) malloc(size);

    size = ((size_t) fs->bitmap_size) * sizeof(uint16_t);
    fs->bitmap = (uint16_t *) malloc(size);

    if (unlikely(!fs->pages || !fs->bitmap)) {
        report_error("fs: create: memory exhausted");
        fs_destroy(fs);
        return FALSE;
    }

    fs->free_pages = 0xFFFF;
    fs->last_sn.word1 = 0;
    fs->last_sn.word2 = 0;

    return TRUE;
}

int fs_load_image(struct fs *fs, const char *filename)
{
    FILE *fp;
    struct page *pg;
    uint16_t vda, j, meta_len;
    uint16_t w, *meta_ptr;
    int c;

    fp = fopen(filename, "rb");
    if (!fp) {
        report_error("fs: load_image: could not open `%s`",
                     filename);
        return FALSE;
    }

    meta_len = PAGE_DATA / sizeof(uint16_t);
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        c = fgetc(fp);
        if (c == EOF) goto error_eof;

        c = fgetc(fp);
        if (c == EOF) goto error_eof;

        /* Discard the first word and use the loop index instead. */
        pg->page_vda = vda;

        meta_ptr = (uint16_t *) pg;
        for (j = 1; j < meta_len; j++) {
            /* Process data in little endian format. */
            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w |= (uint16_t) ((c & 0xFF) << 8);

            meta_ptr[j] = w;
        }

        for (j = 0; j < PAGE_DATA_SIZE; j++) {
            c = fgetc(fp);
            if (c == EOF) goto error_eof;

            /* Byte swap the data here. */
            pg->data[j ^ 1] = (uint8_t) c;
        }
    }

    c = fgetc(fp);
    if (c != EOF) {
        report_error("fs: load_image: "
                     "file `%s` longer than expected", filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("fs: load_image: "
                 "premature end of file in `%s`", filename);
    fclose(fp);
    return FALSE;
}

int fs_save_image(const struct fs *fs, const char *filename)
{
    FILE *fp;
    const struct page *pg;
    uint16_t vda, j, meta_len, w;
    const uint16_t *meta_ptr;
    int c;

    fp = fopen(filename, "wb");
    if (!fp) {
        report_error("fs: save_image: could not open file `%s` "
                     "for writing", filename);
        return FALSE;
    }

    meta_len = PAGE_DATA / sizeof(uint16_t);
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        /* Discard the first word. */
        c = fputc((int) (vda & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((vda >> 8) & 0xFF), fp);
        if (c == EOF) goto error;

        meta_ptr = (const uint16_t *) pg;
        for (j = 1; j < meta_len; j++) {
            w = meta_ptr[j];

            /* Process data in little endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }

        for (j = 0; j < PAGE_DATA_SIZE; j++) {
            /* Byte swap the data here. */
            c = fputc((int) pg->data[j ^ 1], fp);
            if (c == EOF) goto error;
        }
    }

    fclose(fp);
    return TRUE;

error:
    report_error("fs: save_image: error while writing `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int fs_real_to_virtual(const struct fs *fs, uint16_t rda, uint16_t *vda)
{
    uint16_t i, cylinder, head, sector, disk_num;
    const struct geometry *dg;

    cylinder = (rda >> 3) & 0x1FF;
    head = (rda >> 2) & 1;
    sector = (rda >> 12) & 0xF;
    disk_num = (rda >> 1) & 1;

    dg = &fs->dg;
    if ((disk_num >= dg->num_disks) || (cylinder >= dg->num_cylinders)
        || (head >= dg->num_heads) || (sector >= dg->num_sectors)
        || ((rda & 1) != 0))
        return FALSE;

    i = disk_num;
    i = i * dg->num_cylinders + cylinder;
    i = i * dg->num_heads + head;
    i = i * dg->num_sectors + sector;
    *vda = i;
    return TRUE;
}

int fs_virtual_to_real(const struct fs *fs, uint16_t vda, uint16_t *rda)
{
    uint16_t i, cylinder, head, sector, disk_num;
    const struct geometry *dg;

    if (vda >= fs->length) return FALSE;
    dg = &fs->dg;

    i = vda;
    sector = i % dg->num_sectors;
    i /= dg->num_sectors;
    head = i % dg->num_heads;
    i /= dg->num_heads;
    cylinder = i % dg->num_cylinders;
    i /= dg->num_cylinders;
    disk_num = i % dg->num_disks;

    *rda = (cylinder << 3) | (head << 2)
        | (sector << 12) | (disk_num << 1);
    return TRUE;
}

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

        if (!fs_virtual_to_real(fs, vda, &rda)) {
            report_error("fs: check_integrity: could not convert "
                         "virtual to real disk address: %u", vda);
            return FALSE;
        }

        if (pg->label.version == VERSION_FREE
            || pg->label.version == VERSION_BAD
            || pg->label.version == 0)
            continue;

        if (pg->label.prev_rda != 0) {
            if (!fs_real_to_virtual(fs, pg->label.prev_rda, &ovda)) {
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
            if (!fs_real_to_virtual(fs, pg->label.next_rda, &ovda)) {
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
    uint8_t slen;
    int success;

    success = TRUE;
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        if (!fs_virtual_to_real(fs, vda, &rda)) {
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

            slen = pg->data[LEADER_NAME];
            if (slen == 0 || slen >= NAME_LENGTH) {
                report_error("fs: check_integrity: "
                             "invalid name length at VDA = %u: %u",
                             vda, slen);
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

    tr = (struct traverse_files_result *) arg;

    if (type == 1 && fe->leader_vda == 1) {
        uint16_t num_disks, num_cylinders;
        uint16_t num_heads, num_sectors;

        if (length != 5) {
            report_error("fs: check_integrity: "
                         "invalid property length");
            tr->error = TRUE;
            return 1;
        }

        num_disks = read_word_be(data, 0);
        num_cylinders = read_word_be(data, 2);
        num_heads = read_word_be(data, 4);
        num_sectors = read_word_be(data, 6);
        if (num_disks != fs->dg.num_disks
            || num_cylinders != fs->dg.num_cylinders
            || num_heads != fs->dg.num_heads
            || num_sectors != fs->dg.num_sectors) {

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
    uint8_t buffer[32];
    uint16_t num_disks;
    uint16_t num_cylinders;
    uint16_t num_heads;
    uint16_t num_sectors;
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

    num_disks = read_word_be(buffer, DESCRIPTOR_NUM_DISKS);
    num_cylinders = read_word_be(buffer, DESCRIPTOR_NUM_CYLINDERS);
    num_heads = read_word_be(buffer, DESCRIPTOR_NUM_HEADS);
    num_sectors = read_word_be(buffer, DESCRIPTOR_NUM_SECTORS);
    diskbt_size = read_word_be(buffer, DESCRIPTOR_DISKBT_SIZE);

    if (num_disks != fs->dg.num_disks
        || num_cylinders != fs->dg.num_cylinders
        || num_heads != fs->dg.num_heads
        || num_sectors != fs->dg.num_sectors) {

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

void fs_update_metadata(struct fs *fs)
{
    const struct page *pg;
    uint16_t vda, idx, bit;
    uint16_t word1, word2;

    memset(fs->bitmap, -1, fs->bitmap_size * sizeof(uint16_t));
    fs->free_pages = 0;
    fs->last_sn.word1 = 0;
    fs->last_sn.word2 = 0;

    for (vda = 0; vda < fs->length; vda++) {
        idx = IDX(vda);
        bit = BIT(vda);

        pg = &fs->pages[vda];
        if (pg->label.version == VERSION_FREE) {
            fs->bitmap[idx] &= ~(1 << bit);
            fs->free_pages++;
            continue;
        }

        if (pg->label.version == 0
            || pg->label.version == VERSION_BAD)
            continue;

        if (pg->label.file_pgnum == 0) {
            word1 = pg->label.sn.word1 & SN_PART1_MASK;
            word2 = pg->label.sn.word2;
            if (word1 > fs->last_sn.word1
                || ((word1 == fs->last_sn.word1)
                    && (word2 > fs->last_sn.word2))) {

                fs->last_sn.word1 = word1;
                fs->last_sn.word2 = word2;
            }
        }
    }
    fs->last_sn.word2++;
    if (fs->last_sn.word2 == 0) {
        fs->last_sn.word1++;
        fs->last_sn.word1 &= SN_PART1_MASK;
    }
}

int fs_find_free_page(struct fs *fs, uint16_t *free_vda)
{
    const struct page *pg;
    uint16_t idx, bit;
    uint16_t vda;

    while (TRUE) {
        if (fs->free_pages == 0)
            return FALSE;

        for (idx = 0; idx < fs->bitmap_size; idx++) {
            if (fs->bitmap[idx] != 0xFFFF) break;
        }

        if (idx == fs->bitmap_size) {
            /* Something went wrong here, retry. */
            fs_update_metadata(fs);
            continue;
        }

        for (bit = 0; bit < 16; bit++) {
            if (!(fs->bitmap[idx] & (1 << bit)))
                break;
        }

        fs->bitmap[idx] |= (1 << bit);
        fs->free_pages--;

        vda = VDA(idx, bit);
        pg = &fs->pages[vda];
        if (pg->label.version != VERSION_FREE) {
            /* Something went wrong here, retry. */
            fs_update_metadata(fs);
            continue;
        }

        *free_vda = vda;
        return TRUE;
    }

    return FALSE;
}

int fs_file_entry(const struct fs *fs, uint16_t leader_vda,
                  struct file_entry *fe)
{
    const struct page *pg;

    if (leader_vda >= fs->length) {
        report_error("fs: file_entry: "
                     "invalid VDA: %u", leader_vda);
        return FALSE;
    }

    pg = &fs->pages[leader_vda];
    fe->sn = pg->label.sn;
    fe->version = pg->label.version;
    fe->blank = 0;
    fe->leader_vda = leader_vda;

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
    if (len <= DIRECTORY_NAME) {
        report_error("fs: check_directory_entry: "
                     "length of name (%u) is too short",
                     de->length);
        return FALSE;
    }

    if ((de->name_length + DIRECTORY_NAME) > len) {
        report_error("fs: check_directory_entry: "
                     "string buffer overflow: "
                     "name_length = %u, len = %u",
                     de->name_length, len);
        return FALSE;
    }

    return TRUE;
}

int fs_open(const struct fs *fs,
            const struct file_entry *fe,
            struct open_file *of)
{
    if (fe->leader_vda >= fs->length) {
        report_error("fs: open: invalid VDA: %u",
                     fe->leader_vda);
        of->error = TRUE;
        return FALSE;
    }

    of->fe = *fe;
    of->pos.pgnum = 1;
    of->pos.pos = 0;
    of->pos.vda = of->fe.leader_vda;

    of->eof = FALSE;
    of->error = FALSE;
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

int fs_advance_page(const struct fs *fs, struct open_file *of)
{
    const struct page *pg;
    uint16_t vda, rda;

    if (!fs_check_of(fs, of)) {
        report_error("fs: check_eop: error on file");
        return FALSE;
    }

    /* Checks if reached the end of the file. */
    if (of->eof) return TRUE;

    vda = of->pos.vda;
    pg = &fs->pages[vda];

    if (of->pos.pos >= pg->label.nbytes) {
        /* Go to the next page. */
        rda = pg->label.next_rda;
        if (!fs_real_to_virtual(fs, rda, &vda)) {
            of->error = TRUE;
            report_error("fs: check_eop: could not convert real "
                         "to virtual disk address");
            return FALSE;
        }

        if (vda != 0) {
            /* If there is a valid next page. */
            if (vda >= fs->length) {
                of->error = TRUE;
                report_error("fs: check_eop: invalid VDA %u "
                             "for next page", of->pos.vda);
                return FALSE;
            }
            of->pos.vda = vda;
            of->pos.pos = 0;
            of->pos.pgnum += 1;
        } else {
            /* Reached the end of file. */
            of->eof = TRUE;
        }
    }

    return TRUE;
}

size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len)
{
    const struct page *pg;
    uint16_t vda, nbytes;
    size_t pos;

    if (!fs_check_of(fs, of)) {
        report_error("fs: read: error on file");
        return FALSE;
    }

    pos = 0;
    while ((len > 0) && (!of->eof)) {
        vda = of->pos.vda;
        pg = &fs->pages[vda];

        /* If has not reached the end of the page. */
        if (of->pos.pos < pg->label.nbytes) {
            nbytes = pg->label.nbytes - of->pos.pos;
            if (nbytes > len) nbytes = len;

            if (dst) {
                memcpy(&dst[pos], &pg->data[of->pos.pos],
                       nbytes);
            }

            of->pos.pos += nbytes;
            pos += nbytes;
            len -= nbytes;
        }

        if (len == 0)
            break;

        if (!fs_advance_page(fs, of)) {
            report_error("fs: read: could not advance "
                         "to the next page");
            break;
        }
    }

    return pos;
}

size_t fs_write(struct fs *fs, struct open_file *of,
                const uint8_t *src, size_t len, int extend)
{
    struct page *pg, *npg;
    uint16_t vda, nbytes;
    size_t pos;

    if (!fs_check_of(fs, of)) {
        report_error("fs: write: error on file");
        return FALSE;
    }

    pos = 0;
    while ((len > 0) && (!of->eof)) {
        vda = of->pos.vda;
        pg = &fs->pages[vda];

        /* If has not reached the end of the page. */
        if (of->pos.pos < pg->label.nbytes) {
            nbytes = pg->label.nbytes - of->pos.pos;
            if (nbytes > len) nbytes = len;

            if (src) {
                memcpy(&pg->data[of->pos.pos], &src[pos], nbytes);
            }

            of->pos.pos += nbytes;
            pos += nbytes;
            len -= nbytes;
        }

        if (of->pos.pos < pg->label.nbytes)
            break;

        /* Go to the next page. */
        if (!fs_advance_page(fs, of)) {
            report_error("fs: write: could not advance "
                         "to the next page");
            break;
        }

        /* If there is a valid next page. */
        if (!of->eof) continue;

        /* Reached the end of file. */
        if (!extend) break;

        of->eof = FALSE;

        /* Check if the last page can be extended. */
        if (pg->label.nbytes < PAGE_DATA_SIZE) {
            nbytes = PAGE_DATA_SIZE - pg->label.nbytes;
            if (nbytes > len) nbytes = len;
            pg->label.nbytes += nbytes;
            continue;
        }

        /* Otherwise, allocate a new page. */
        if (!fs_find_free_page(fs, &vda)) {
            of->error = TRUE;
            report_error("fs: write: disk full");
            break;
        }

        npg = &fs->pages[vda];
        if (!fs_virtual_to_real(fs, pg->page_vda,
                                &npg->label.prev_rda)) {
            of->error = TRUE;
            report_error("fs: write: could not convert virtual "
                         "to real disk address");
            break;
        }

        if (!fs_virtual_to_real(fs, npg->page_vda,
                                &pg->label.next_rda)) {
            of->error = TRUE;
            report_error("fs: write: could not convert virtual "
                         "to real disk address");
            break;
        }

        nbytes = len;
        if (nbytes > PAGE_DATA_SIZE)
            nbytes = PAGE_DATA_SIZE;

        npg->label.next_rda = 0;
        npg->label.nbytes = nbytes;
        npg->label.file_pgnum = pg->label.file_pgnum + 1;
        npg->label.version = pg->label.version;
        npg->label.sn = pg->label.sn;
        of->pos.vda = vda;
        of->pos.pos = 0;
        of->pos.pgnum += 1;
    }

    return pos;
}

int fs_trim(struct fs *fs, struct open_file *of)
{
    struct page *pg, *first_pg;

    if (!fs_advance_page(fs, of)) {
        report_error("fs: trim: could not advance first page");
        return FALSE;
    }

    first_pg = &fs->pages[of->pos.vda];
    first_pg->label.nbytes = of->pos.pos;

    if (of->eof) return TRUE;

    while (TRUE) {
        if (!fs_advance_page(fs, of)) {
            report_error("fs: trim: could not advance page");
            first_pg->label.next_rda = 0;
            return FALSE;
        }
        if (of->eof) break;
        pg = &fs->pages[of->pos.vda];
        pg->label.version = VERSION_FREE;
        of->pos.pos = pg->label.nbytes;
    }

    first_pg->label.next_rda = 0;
    return TRUE;
}

int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length, struct open_file *end_of)
{
    struct open_file of;
    size_t l, nbytes;

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: file_length: could not open file");
        return FALSE;
    }

    /* Skip the leader page. */
    nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: file_length: error while reading");
        return FALSE;
    }

    l = 0;
    while (TRUE) {
        nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
        if (of.error) {
            report_error("fs: file_length: error while reading");
            return FALSE;
        }

        l += nbytes;
        if (nbytes != PAGE_DATA_SIZE) break;
    }

    *length = l;
    if (end_of) *end_of = of;
    return TRUE;
}

/* Auxiliary function to read the leader page.
 * The file to read the leader page is given in parameter `fe`.
 * The page is stored in `data`.
 * Returns TRUE on success.
 */
static
int read_leader_page(const struct fs *fs,
                     const struct file_entry *fe,
                     uint8_t data[PAGE_DATA_SIZE])
{
    struct open_file of;
    size_t nbytes;

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: read_leader_page: "
                     "could not open file");
        return FALSE;
    }

    nbytes = fs_read(fs, &of, data, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: read_leader_page: "
                     "could not read leader page");
        return FALSE;
    }
    return TRUE;
}

int fs_file_info(const struct fs *fs,
                 const struct file_entry *fe,
                 struct file_info *finfo)
{
    uint8_t data[PAGE_DATA_SIZE];

    if (!read_leader_page(fs, fe, data)) {
        report_error("fs: file_info: "
                     "could not read leader page");
        return FALSE;
    }

    finfo->name_length = data[LEADER_NAME];
    copy_name(finfo->name, (const char *) &data[LEADER_NAME]);
    finfo->created = read_alto_time(data, LEADER_CREATED);
    finfo->written = read_alto_time(data, LEADER_WRITTEN);
    finfo->read = read_alto_time(data, LEADER_READ);

    finfo->propbegin = data[LEADER_PROPBEGIN];
    finfo->proplen = data[LEADER_PROPLEN];
    finfo->consecutive = data[LEADER_CONSECUTIVE];
    finfo->change_sn = data[LEADER_CHANGESN];

    finfo->fe.sn.word1 = read_word_be(data, LEADER_DIRFPHINT);
    finfo->fe.sn.word2 = read_word_be(data, LEADER_DIRFPHINT + 2);
    finfo->fe.version = read_word_be(data, LEADER_DIRFPHINT + 4);
    finfo->fe.blank = read_word_be(data, LEADER_DIRFPHINT + 6);
    finfo->fe.leader_vda = read_word_be(data, LEADER_DIRFPHINT + 8);

    finfo->last_page.vda = read_word_be(data, LEADER_LASTPAGEHINT);
    finfo->last_page.pgnum = read_word_be(data, LEADER_LASTPAGEHINT + 2);
    finfo->last_page.pos = read_word_be(data, LEADER_LASTPAGEHINT + 4);
    return TRUE;
}

int fs_scan_properties(const struct fs *fs,
                       const struct file_entry *fe,
                       scan_property_cb cb, void *arg)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    const uint8_t *data;
    uint8_t type, length;
    size_t i, nbytes;
    int ret;

    if (!read_leader_page(fs, fe, buffer)) {
        report_error("fs: scan_properties: "
                     "could not read leader page");
        return FALSE;
    }

    if (buffer[LEADER_PROPBEGIN] == 0) return TRUE;

    if (2 * buffer[LEADER_PROPBEGIN] != LEADER_PROPS) {
        report_error("fs: scan_properties: "
                     "PROPBEGIN = %u != %u",
                     2 * buffer[LEADER_PROPBEGIN], LEADER_PROPS);
        return FALSE;
    }

    nbytes = 2 * buffer[LEADER_PROPLEN];
    if (nbytes > (LEADER_SPARE - LEADER_PROPS)) {
        report_error("fs: scan_properties: "
                     "invalid PROPLEN = %u",
                     buffer[LEADER_PROPLEN]);
        return FALSE;
    }

    data = &buffer[LEADER_PROPS];

    i = 0;
    while (i < nbytes) {
        type = data[i++];
        if (i == nbytes) {
            report_error("fs: scan_properties: "
                         "missing length");
            return FALSE;
        }
        length = data[i++];

        if (2 * length + i > nbytes) {
            report_error("fs: scan_properties: "
                         "overflow");
            return FALSE;
        }

        ret = cb(fs, fe, type, length, &data[i], arg);
        if (ret < 0) {
            report_error("fs: scan_properties: error while scanning");
            return FALSE;
        }
        if (ret == 0) break;

        i += 2 * length;
    }

    return TRUE;
}

/* Auxiliary callback used by fs_find_file().
 * The `arg` parameter is a pointer to find_result structure.
 */
static
int find_file_cb(const struct fs *fs,
                 const struct directory_entry *de,
                 void *arg)
{
    struct find_result *res;
    struct file_info finfo;

    if (de->type == DIR_ENTRY_MISSING) {
        /* Skip missing entries (but do not stop). */
        return 1;
    }

    res = (struct find_result *) arg;
    if (!fs_file_info(fs, &de->fe, &finfo)) {
        report_error("fs: find_file: could not get file information");
        return -1;
    }

    if (strncmp(finfo.name, res->name, res->name_length) == 0) {
        res->fe = de->fe;
        res->found = TRUE;
        /* Stop the search in this directory. */
        return 0;
    }
    return 1;
}

int fs_find_file(const struct fs *fs, const char *name,
                 struct file_entry *fe, struct file_entry *dir_fe)
{
    struct find_result res;
    struct file_entry root_fe;
    struct file_entry _fe, _dir_fe;
    size_t pos, npos;

    if (!fs_file_entry(fs, 1, &root_fe)) {
        report_error("fs: find_file: "
                     "error finding SysDir at page 1");
        return FALSE;
    }

    pos = 0;
    _fe = _dir_fe = root_fe;
    while (name[pos]) {
        if (name[pos] == '<') {
            _fe = _dir_fe = root_fe;
            pos++;
            continue;
        }

        npos = pos + 1;
        while (name[npos]) {
            if (name[npos] == '<' || name[npos] == '>')
                break;
            npos++;
        }

        res.name = &name[pos];
        res.name_length = npos - pos;
        res.found = FALSE;

        if (res.name_length >= NAME_LENGTH) {
            report_error("fs: find_file: name too long");
            return FALSE;
        }

        if (!fs_scan_directory(fs, &_fe, &find_file_cb, &res)) {
            report_error("fs: find_file: could not scan directory");
            return FALSE;
        }

        if (!res.found) return FALSE;
        _dir_fe = _fe;
        _fe = res.fe;

        if (name[npos] == '>') npos++;
        pos = npos;
    }

    *fe = _fe;
    if (dir_fe) {
        *dir_fe = _dir_fe;
    }

    return TRUE;
}

int fs_scan_files(const struct fs *fs, scan_files_cb cb, void *arg)
{
    uint16_t vda;
    const struct page *pg;
    struct file_entry fe;
    int ret;

    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];
        if (pg->label.file_pgnum != 0) continue;
        if (pg->label.version == VERSION_FREE) continue;
        if (pg->label.version == VERSION_BAD) continue;
        if (pg->label.version == 0) continue;

        fe.sn = pg->label.sn;
        fe.version = pg->label.version;
        fe.blank = 0;
        fe.leader_vda = vda;

        ret = cb(fs, &fe, arg);
        if (ret < 0) {
            report_error("fs: scan_files: error while scanning");
            return FALSE;
        }
        if (ret == 0) break;
    }

    return TRUE;
}

int fs_scan_directory(const struct fs *fs, const struct file_entry *fe,
                      scan_directory_cb cb, void *arg)
{
    struct directory_entry de;
    struct open_file of;
    uint16_t w;
    uint8_t buffer[128];
    size_t to_read, nbytes;
    int ret;

    if (!(fe->sn.word1 & SN_DIRECTORY)) {
        report_error("fs: scan_directory: "
                     "file_entry does not point to a directory");
        return FALSE;
    }

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: scan_directory: "
                     "could not open directory");
        return FALSE;
    }

    /* Skip the leader page. */
    nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: scan_directory: error while reading");
        return FALSE;
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, 2);
        if (of.error) goto error_read;

        if (nbytes == 0) break;
        if (nbytes != 2) goto error_short;

        w = read_word_be(buffer, 0);
        de.type = (w >> DIR_ENTRY_TYPE_SHIFT);

        de.length = (w & DIR_ENTRY_LEN_MASK);
        to_read = 2 * ((size_t) de.length);

        if (to_read > sizeof(buffer)) {
            nbytes = fs_read(fs, &of, &buffer[2], sizeof(buffer) - 2);
            if (of.error) goto error_read;
            if (nbytes != sizeof(buffer) - 2) goto error_short;
            to_read -= sizeof(buffer);

            /* Discard the remaining data. */
            nbytes = fs_read(fs, &of, NULL, to_read);
            if (of.error) goto error_read;
            if (nbytes != to_read) goto error_short;
        } else {
            nbytes = fs_read(fs, &of, &buffer[2], to_read - 2);
            if (of.error) goto error_read;
            if (nbytes != to_read - 2) goto error_short;
        }

        de.fe.sn.word1 = read_word_be(buffer, DIRECTORY_SN);
        de.fe.sn.word2 = read_word_be(buffer, 2 + DIRECTORY_SN);
        de.fe.version = read_word_be(buffer, DIRECTORY_VERSION);
        de.fe.blank = 0;
        de.fe.leader_vda = read_word_be(buffer, DIRECTORY_LEADER_VDA);
        de.name_length = buffer[DIRECTORY_NAME];
        copy_name(de.name, (const char *) &buffer[DIRECTORY_NAME]);

        ret = cb(fs, &de, arg);
        if (ret < 0) {
            report_error("fs: scan_directory: error while scanning");
            return FALSE;
        }
        if (ret == 0) break;
    }

    return TRUE;

error_read:
    report_error("fs: scan_directory: error while reading");
    return FALSE;

error_short:
    report_error("fs: scan_directory: entry too short");
    return FALSE;
}

int fs_update_descriptor(struct fs *fs)
{
    struct file_entry fe;
    struct open_file of;
    uint8_t buffer[32];
    size_t nbytes;
    uint16_t i;

    fs_update_metadata(fs);

    if (!fs_find_file(fs, "DiskDescriptor", &fe, NULL)) {
        report_error("fs: update_descriptor: "
                     "could not find DiskDescriptor");
        return FALSE;
    }

    if (!fs_open(fs, &fe, &of)) {
        report_error("fs: update_descriptor: "
                     "could not open DiskDescriptor");
        return FALSE;
    }
    /* Skip the leader page. */
    nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: update_descriptor: "
                     "could not skip leader page");
        return FALSE;
    }

    memset(buffer, 0, sizeof(buffer));
    write_word_be(buffer, DESCRIPTOR_NUM_DISKS, fs->dg.num_disks);
    write_word_be(buffer, DESCRIPTOR_NUM_CYLINDERS, fs->dg.num_cylinders);
    write_word_be(buffer, DESCRIPTOR_NUM_HEADS, fs->dg.num_heads);
    write_word_be(buffer, DESCRIPTOR_NUM_SECTORS, fs->dg.num_sectors);
    write_word_be(buffer, DESCRIPTOR_LAST_SN, fs->last_sn.word1);
    write_word_be(buffer, DESCRIPTOR_LAST_SN + 2, fs->last_sn.word2);
    write_word_be(buffer, DESCRIPTOR_DISKBT_SIZE, fs->bitmap_size);
    write_word_be(buffer, DESCRIPTOR_VERSIONS_KEPT, 0);
    write_word_be(buffer, DESCRIPTOR_FREE_PAGES, fs->free_pages);

    nbytes = fs_write(fs, &of, buffer, sizeof(buffer), TRUE);
    if (nbytes != sizeof(buffer) || of.error) {
        report_error("fs: update_descriptor: "
                     "could not write");
        return FALSE;
    }

    for (i = 0; i < fs->bitmap_size; i++) {
        write_word_be(buffer, 0, fs->bitmap[i]);
        nbytes = fs_write(fs, &of, buffer, 2, TRUE);
        if (nbytes != 2 || of.error) {
            report_error("fs: update_descriptor: "
                         "could not write");
            return FALSE;
        }
    }
    if (!fs_trim(fs, &of)) {
        report_error("fs: update_descriptor: "
                     "could not trim file");
        return FALSE;
    }

    return TRUE;
}

int fs_update_leader_page(struct fs *fs, const struct file_entry *fe)
{
    struct open_file of, end_of;
    uint8_t data[PAGE_DATA_SIZE];
    size_t nbytes, length;

    if (!read_leader_page(fs, fe, data)) {
        report_error("fs: update_leader_page: "
                     "could not read leader page");
        return FALSE;
    }

    if (!fs_file_length(fs, fe, &length, &end_of)) {
        report_error("fs: update_leader_page: "
                     "could not determine length");
        return FALSE;
    }

    write_word_be(data, LEADER_DIRFPHINT, fe->sn.word1);
    write_word_be(data, LEADER_DIRFPHINT + 2, fe->sn.word2);
    write_word_be(data, LEADER_DIRFPHINT + 4, fe->version);
    write_word_be(data, LEADER_DIRFPHINT + 6, 0); /* blank. */
    write_word_be(data, LEADER_DIRFPHINT + 8, fe->leader_vda);

    write_word_be(data, LEADER_LASTPAGEHINT, end_of.pos.vda);
    write_word_be(data, LEADER_LASTPAGEHINT + 2, end_of.pos.pgnum);
    write_word_be(data, LEADER_LASTPAGEHINT + 4, end_of.pos.pos);

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: update_leader_page: "
                     "could not open file");
        return FALSE;
    }

    nbytes = fs_write(fs, &of, data, PAGE_DATA_SIZE, FALSE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: update_leader_page: "
                     "could not write leader page");
        return FALSE;
    }

    return TRUE;
}

int fs_extract_file(const struct fs *fs, const struct file_entry *fe,
                    const char *output_filename, int include_leader_page)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    struct open_file of;
    FILE *fp;
    size_t nbytes;
    size_t ret;

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: extract_file: "
                     "could not open file");
        return FALSE;
    }

    fp = fopen(output_filename, "wb");
    if (!fp) {
        report_error("fs: extract_file: could not open `%s` "
                     "for writing", output_filename);
        return FALSE;
    }

    if (!include_leader_page) {
        /* Skip the leader page. */
        nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
        if (nbytes != PAGE_DATA_SIZE || of.error) {
            report_error("fs: extract_file: "
                         "error while discarding leader page");
            fclose(fp);
            return FALSE;
        }
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
        if (of.error) {
            report_error("fs: extract_file: error while reading");
            fclose(fp);
            return FALSE;
        }

        if (nbytes > 0) {
            ret = fwrite(buffer, 1, nbytes, fp);
            if (ret != nbytes) {
                report_error("fs: extract_file: error while writing "
                             "`%s`", output_filename);
                fclose(fp);
                return FALSE;
            }
        }

        if (nbytes < sizeof(buffer)) break;
    }

    fclose(fp);
    return TRUE;
}

/* Copies the name from `srt` to `dst` and set the proper
 * NUL byte at the end of the string.
 */
static
void copy_name(char *dst, const char *src)
{
    uint8_t slen;

    slen = (uint8_t) src[0];
    if (slen >= NAME_LENGTH)
        slen = NAME_LENGTH - 1;

    if (slen == 0) {
        dst[0] = '\0';
        return;
    }

    memcpy(dst, &src[1], ((size_t) slen) - 1);
    dst[slen - 1] = '\0';
}

uint16_t read_word_be(const uint8_t *data, size_t offset)
{
    uint16_t w;
    w = (uint16_t) (data[offset + 1]);
    w |= (uint16_t) (data[offset] << 8);
    return w;
}

void write_word_be(uint8_t *data, size_t offset, uint16_t w)
{
    data[offset + 1] = (uint8_t) w;
    data[offset] = (uint8_t) (w >> 8);
}

time_t read_alto_time(const uint8_t *data, size_t offset)
{
    time_t time;

    time = (int) read_word_be(data, offset + 2);
    time += ((int) read_word_be(data, offset)) << 16;

    time += 2117503696; /* magic value to convert to Unix epoch. */
    return time;
}
