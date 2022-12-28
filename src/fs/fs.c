
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by traverse_dirs_cb(). */
struct traverse_result {
    struct file_entry dir_fe;     /* Current directory. */
    unsigned int count;           /* The entry count so far. */
    int found_missing;            /* Already found missing entries. */
};

/* Auxiliary data structure used by fs_find_file(). */
struct find_result {
    const char *filename;         /* The name of the searched file. */
    size_t flen;                  /* Length of the filename. */
    struct file_entry fe;         /* The file_entry of the file. */
    int found;                    /* If the file was found. */
};

/* Auxiliary data structure used by fs_scavenge_file(). */
struct scavenge_result {
    const char *filename;         /* The name of the searched file. */
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
#define LEADER_FILENAME                  12U
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
#define DIRECTORY_FILENAME               12U

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

/* Forward declarations. */
static int real_to_virtual(const struct fs *fs, uint16_t rda,
                           uint16_t *vda);
static int virtual_to_real(const struct fs *fs, uint16_t vda,
                           uint16_t *rda);
static void copy_name(char *dst, const char *src);
static uint16_t read_word_bs(const uint8_t *data, size_t offset);
static time_t read_alto_time(const uint8_t *data, size_t offset);

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

/* Auxiliary function to fs_check_integrity() [stage1].
 * Checks some basic filesystem consistency.
 * Returns TRUE on success.
 */
static
int check_integrity_stage1(const struct fs *fs)
{
    const struct page *pg, *other_pg;
    uint16_t vda, rda, other_vda;
    uint8_t slen;
    int success;

    success = TRUE;
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        if (!virtual_to_real(fs, vda, &rda)) {
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
                             "invalid bad page at VDA = %u", vda);
                success = FALSE;
            }
            continue;
        }

        if (pg->label.version == 0) {
            report_error("fs: check_integrity: "
                         "invalid label version at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.nbytes > PAGE_DATA_SIZE) {
            report_error("fs: check_integrity: "
                         "invalid label used bytes at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.prev_rda != 0) {
            if (!real_to_virtual(fs, pg->label.prev_rda, &other_vda)) {
                report_error("fs: check_integrity: "
                             "invalid prev_rda at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            other_pg = &fs->pages[other_vda];
            if (other_pg->label.file_pgnum + 1 != pg->label.file_pgnum) {
                report_error("fs: check_integrity: "
                             "discontiguous file_pgnum (backwards) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (other_pg->label.sn.word1 != pg->label.sn.word1
                || other_pg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_integrity: "
                             "differing file serial numbers (backwards) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            /* First page is special, so not test it. */
            if (other_pg->label.next_rda != rda && vda != 0) {
                report_error("fs: check_integrity: "
                             "broken link (backwards) at VDA = %u",
                             vda);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "short leader page at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (pg->label.file_pgnum != 0) {
                report_error("fs: check_integrity: "
                             "file_pgnum is not zero at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            slen = pg->data[LEADER_FILENAME];
            if (slen == 0 || slen >= FILENAME_LENGTH) {
                report_error("fs: check_integrity: "
                             "invalid filename at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "short page at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (!real_to_virtual(fs, pg->label.next_rda, &other_vda)) {
                report_error("fs: check_integrity: "
                             "invalid next_rda at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            other_pg = &fs->pages[other_vda];
            if (other_pg->label.file_pgnum != pg->label.file_pgnum + 1) {
                report_error("fs: check_integrity: "
                             "discontiguous file_pgnum (forward) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (other_pg->label.sn.word1 != pg->label.sn.word1
                || other_pg->label.sn.word2 != pg->label.sn.word2) {
                report_error("fs: check_integrity: "
                             "differing file serial numbers (forward) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            /* First page is special, so not test it. */
            if (other_pg->label.prev_rda != rda && vda != 0) {
                report_error("fs: check_integrity: "
                             "broken link (forward) at VDA = %u",
                             vda);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.prev_rda != 0) continue;
    }

    return success;
}

/* Auxiliary callback used by check_integrity_stage2().
 * The `arg` parameter is a pointer to find_result structure.
 */
static
int traverse_dirs_cb(const struct fs *fs,
                     const struct directory_entry *de,
                     void *arg)
{
    struct traverse_result *tr;
    struct traverse_result child_tr;
    struct file_entry fe;

    tr = (struct traverse_result *) arg;
    tr->count++;

    if (de->type == DIR_ENTRY_MISSING) {
        tr->found_missing = TRUE;
        return 1;
    }

    if (tr->found_missing) {
        report_error("fs: check_integrity: "
                     "missing entry in middle of directory: "
                     "directory entry %u at VDA %u",
                     tr->count, tr->dir_fe.leader_vda);
        /* return -1; */
    }

    if (!fs_file_entry(fs, de->fe.leader_vda, &fe)) {
        report_error("fs: check_integrity: "
                     "invalid leader page at VDA %u: "
                     "directory entry %u at VDA %u",
                     de->fe.leader_vda,
                     tr->count, tr->dir_fe.leader_vda);
        return -1;
    }

    if (memcmp(&fe, &de->fe, sizeof(struct file_entry)) != 0) {
        report_error("fs: check_integrity: "
                     "file entries do not match: "
                     "directory entry %u at VDA %u",
                     tr->count, tr->dir_fe.leader_vda);
        return -1;
    }

    if (fe.sn.word1 & DIRECTORY_SN) {
        child_tr.dir_fe = fe;
        child_tr.count = 0;
        child_tr.found_missing = FALSE;

        if (!fs_scan_directory(fs, &fe, &traverse_dirs_cb, &child_tr))
            return -1;
    }

    return 1;
}

/* Auxiliary function to fs_check_integrity() [stage2].
 * Checks SysDir and its sub-directories.
 * Returns TRUE on success.
 */
static
int check_integrity_stage2(const struct fs *fs)
{
    struct file_entry root_fe;
    struct traverse_result tr;

    if (!fs_file_entry(fs, 1, &root_fe)) {
        report_error("fs: check_integrity: "
                     "error finding SysDir at page 1");
        return FALSE;
    }

    tr.dir_fe = root_fe;
    tr.count = 0;
    tr.found_missing = FALSE;
    if (!fs_scan_directory(fs, &root_fe,
                           &traverse_dirs_cb, &tr)) {
        report_error("fs: check_integrity: "
                     "could not traverse SysDir");
        return FALSE;
    }
    return TRUE;
}

/* Auxiliary function to fs_check_integrity() [stage3].
 * Checks the disk geometry.
 * Returns TRUE on success.
 */
static
int check_integrity_stage3(const struct fs *fs)
{
    struct file_entry fe;
    struct open_file of;
    uint8_t buffer[32];
    uint16_t num_disks;
    uint16_t num_cylinders;
    uint16_t num_heads;
    uint16_t num_sectors;
    uint16_t diskbt_size;
    uint16_t expected_size;
    size_t nbytes;

    if (!fs_find_file(fs, "DiskDescriptor", &fe)) {
        report_error("fs: check_integrity: "
                     "could not find DiskDescriptor");
        return FALSE;
    }

    if (!fs_open(fs, &fe, &of, FALSE)) {
        report_error("fs: check_integrity: "
                     "could not open DiskDescriptor");
        return FALSE;
    }

    nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
    if (nbytes != sizeof(buffer)) {
        report_error("fs: check_integrity: "
                     "could not read DiskDescriptor");
        return FALSE;
    }

    num_disks = read_word_bs(buffer, DESCRIPTOR_NUM_DISKS);
    num_cylinders = read_word_bs(buffer, DESCRIPTOR_NUM_CYLINDERS);
    num_heads = read_word_bs(buffer, DESCRIPTOR_NUM_HEADS);
    num_sectors = read_word_bs(buffer, DESCRIPTOR_NUM_SECTORS);
    diskbt_size = read_word_bs(buffer, DESCRIPTOR_DISKBT_SIZE);

    if (num_disks != fs->dg.num_disks
        || num_cylinders != fs->dg.num_cylinders
        || num_heads != fs->dg.num_heads
        || num_sectors != fs->dg.num_sectors) {

        report_error("fs: check_integrity: "
                     "invalid disk geometry");
        return FALSE;
    }

    expected_size = (fs->length >> 4);
    if ((fs->length & 0xF) != 0) expected_size++;
    if (diskbt_size != expected_size) {
        report_error("fs: check_integrity: "
                     "invalid disk bitmap size");
        return FALSE;
    }
    return TRUE;
}

int fs_check_integrity(const struct fs *fs, int level)
{
    if (level < 0) level = 3;

    if (level <= 0) return TRUE;
    if (!check_integrity_stage1(fs))
        return FALSE;

    if (level <= 1) return TRUE;
    if (!check_integrity_stage2(fs))
        return FALSE;

    if (level <= 2) return TRUE;
    if (!check_integrity_stage3(fs))
        return FALSE;

    return TRUE;
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
    if (pg->label.version == VERSION_FREE) {
        report_error("fs: file_entry: "
                     "free page at VDA %u", leader_vda);
        return FALSE;
    }

    if (pg->label.version == VERSION_BAD) {
        report_error("fs: file_entry: "
                     "bad page at VDA %u", leader_vda);
        return FALSE;
    }

    if (pg->label.version == 0) {
        report_error("fs: file_entry: "
                     "invalid version at VDA %u", leader_vda);
        return FALSE;
    }

    if (pg->label.file_pgnum != 0) {
        report_error("fs: file_entry: "
                     "not the first page at VDA %u", leader_vda);
        return FALSE;
    }

    fe->sn = pg->label.sn;
    fe->version = pg->label.version;
    fe->blank = 0;
    fe->leader_vda = leader_vda;

    return TRUE;
}

int fs_open(const struct fs *fs, const struct file_entry *fe,
            struct open_file *of, int include_leader)
{
    const struct page *pg;
    uint16_t rda;

    if (fe->leader_vda >= fs->length) {
        of->error = TRUE;
        return FALSE;
    }

    of->fe = *fe;
    of->pos.pgnum = 1;
    of->pos.pos = 0;

    if (!include_leader) {
        pg = &fs->pages[fe->leader_vda];
        rda = pg->label.next_rda;
        if (!real_to_virtual(fs, rda, &of->pos.vda)) {
            of->error = TRUE;
            return FALSE;
        }
    } else {
        of->pos.vda = of->fe.leader_vda;
    }

    of->error = FALSE;
    return TRUE;
}

size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len)
{
    const struct page *pg;
    uint16_t vda, rda, nbytes;
    size_t pos;

    if (of->error) {
        report_error("fs: read: error on file");
        return FALSE;
    }

    pos = 0;
    while (len > 0) {
        vda = of->pos.vda;

        /* Checks if reached the end of the file. */
        if (vda == 0) break;
        if (vda >= fs->length) {
            of->error = TRUE;
            report_error("fs: read: invalid VDA: %u", vda);
            break;
        }

        pg = &fs->pages[vda];

        /* Sanity check. */
        if (pg->label.file_pgnum != of->pos.pgnum) {
            of->error = TRUE;
            report_error("fs: read: inconsistent page numbers");
            break;
        }

        /* Another sanity check. */
        if (of->pos.pos > pg->label.nbytes) {
            of->error = TRUE;
            report_error("fs: read: inconsistent offset in page");
            break;
        }

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
            continue;
        }

        /* Go to the next page. */
        rda = pg->label.next_rda;
        if (!real_to_virtual(fs, rda, &of->pos.vda)) {
            of->error = TRUE;
            report_error("fs: read: could not convert real "
                         "to virtual disk address");
            break;
        }

        /* If there is a valid next page. */
        if (of->pos.vda != 0) {
            of->pos.pos = 0;
            of->pos.pgnum += 1;
            continue;
        }

        /* Reached the end of file. */
        of->pos.pgnum = 0;
    }

    return pos;
}

int fs_find_free_page(struct fs *fs, uint16_t *free_vda)
{
    uint16_t vda;
    const struct page *pg;

    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];
        if (pg->label.version == VERSION_FREE) {
            *free_vda = vda;
            return TRUE;
        }
    }
    return FALSE;
}

size_t fs_write(struct fs *fs, struct open_file *of,
                const uint8_t *src, size_t len, int extend)
{
    struct page *pg, *new_pg;
    uint16_t vda, rda, nbytes;
    size_t pos;

    if (of->error) {
        report_error("fs: write: error on file");
        return FALSE;
    }

    pos = 0;
    while (len > 0) {
        vda = of->pos.vda;

        /* Checks if reached the end of the file. */
        if (vda == 0) break;
        if (vda >= fs->length) {
            of->error = TRUE;
            report_error("fs: write: invalid VDA: %u", vda);
            break;
        }

        pg = &fs->pages[vda];

        /* Sanity check. */
        if (pg->label.file_pgnum != of->pos.pgnum) {
            of->error = TRUE;
            report_error("fs: write: inconsistent page numbers");
            break;
        }

        /* Another sanity check. */
        if (of->pos.pos > pg->label.nbytes) {
            of->error = TRUE;
            report_error("fs: write: inconsistent offset in page");
            break;
        }

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
            continue;
        }

        /* Go to the next page. */
        rda = pg->label.next_rda;
        if (!real_to_virtual(fs, rda, &of->pos.vda)) {
            of->error = TRUE;
            report_error("fs: write: could not convert real "
                         "to virtual disk address");
            break;
        }


        /* If there is a valid next page. */
        if (of->pos.vda != 0) {
            of->pos.pos = 0;
            of->pos.pgnum += 1;
            continue;
        }

        if (!extend) break;

        /* Check if the last page can be extended. */
        of->pos.vda = vda;
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

        new_pg = &fs->pages[vda];

        if (!virtual_to_real(fs, pg->page_vda, &new_pg->label.prev_rda)) {
            of->error = TRUE;
            report_error("fs: write: could not convert virtual "
                         "to real disk address");
            break;
        }

        if (!virtual_to_real(fs, new_pg->page_vda, &pg->label.next_rda)) {
            of->error = TRUE;
            report_error("fs: write: could not convert virtual "
                         "to real disk address");
            break;
        }

        nbytes = len;
        if (nbytes > PAGE_DATA_SIZE)
            nbytes = PAGE_DATA_SIZE;

        new_pg->label.next_rda = 0;
        new_pg->label.nbytes = nbytes;
        new_pg->label.file_pgnum = pg->label.file_pgnum + 1;
        new_pg->label.version = pg->label.version;
        new_pg->label.sn = pg->label.sn;
    }

    return pos;
}

int fs_trim(struct fs *fs, struct open_file *of)
{
    struct page *pg;
    uint16_t vda, rda;
    int should_keep;

    if (of->error) {
        report_error("fs: trim: error on file");
        return FALSE;
    }

    should_keep = TRUE;
    vda = of->pos.vda;
    while (vda != 0) {
        if (vda >= fs->length) {
            of->error = TRUE;
            report_error("fs: trim: invalid VDA: %u", vda);
            break;
        }

        pg = &fs->pages[vda];
        rda = pg->label.next_rda;

        if (!should_keep) {
            /* Remove the page. */
            pg->label.version = VERSION_FREE;
            pg->label.prev_rda = 0;
            pg->label.next_rda = 0;
        }

        if (vda == of->pos.vda) {
            pg->label.nbytes = of->pos.pos;
            /* If the current page is full, keep the next page. */
            if (pg->label.nbytes != PAGE_DATA_SIZE) {
                pg->label.next_rda = 0;
                should_keep = FALSE;
            }
        } else if (should_keep) {
            /* Set nbytes to zero in the page after the current. */
            pg->label.nbytes = 0;
            pg->label.next_rda = 0;
            should_keep = FALSE;
        }

        /* Go to the next page. */
        if (!real_to_virtual(fs, rda, &vda)) {
            of->error = TRUE;
            report_error("fs: trim: could not convert real "
                         "to virtual disk address");
            break;
        }
    }

    return TRUE;
}

int fs_extract_file(const struct fs *fs, const struct file_entry *fe,
                    const char *output_filename)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    struct open_file of;
    FILE *fp;
    size_t nbytes;
    size_t ret;

    if (!fs_open(fs, fe, &of, FALSE)) {
        report_error("fs: extract_file: could not open filesystem file");
        return FALSE;
    }

    fp = fopen(output_filename, "wb");
    if (!fp) {
        report_error("fs: extract_file: could not open `%s` "
                     "for writing", output_filename);
        return FALSE;
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

int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length)
{
    struct open_file of;
    size_t l, nbytes;

    if (!fs_open(fs, fe, &of, FALSE)) {
        report_error("fs: file_length: could not open filesystem file");
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
    return TRUE;
}

int fs_file_info(const struct fs *fs, const struct file_entry *fe,
                 struct file_info *finfo)
{
    const struct page *pg;

    if (fe->leader_vda >= fs->length) {
        report_error("fs: file_info: invalid VDA: %u",
                     fe->leader_vda);
        return FALSE;
    }

    pg = &fs->pages[fe->leader_vda];
    copy_name(finfo->filename, (const char *) &pg->data[LEADER_FILENAME]);
    finfo->created = read_alto_time(pg->data, LEADER_CREATED);
    finfo->written = read_alto_time(pg->data, LEADER_WRITTEN);
    finfo->read = read_alto_time(pg->data, LEADER_READ);

    finfo->consecutive = pg->data[LEADER_CONSECUTIVE];
    finfo->change_sn = pg->data[LEADER_CHANGESN];

    finfo->fe.sn.word1 = read_word_bs(pg->data, LEADER_DIRFPHINT);
    finfo->fe.sn.word2 = read_word_bs(pg->data, LEADER_DIRFPHINT + 2);
    finfo->fe.version = read_word_bs(pg->data, LEADER_DIRFPHINT + 4);
    finfo->fe.blank = read_word_bs(pg->data, LEADER_DIRFPHINT + 6);
    finfo->fe.leader_vda = read_word_bs(pg->data, LEADER_DIRFPHINT + 8);

    finfo->last_page.vda = read_word_bs(pg->data, LEADER_LASTPAGEHINT);
    finfo->last_page.pgnum = read_word_bs(pg->data, LEADER_LASTPAGEHINT + 2);
    finfo->last_page.pos = read_word_bs(pg->data, LEADER_LASTPAGEHINT + 4);
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

    if (strncmp(finfo.filename, res->filename, res->flen) == 0) {
        res->fe = de->fe;
        res->found = TRUE;
        /* Stop the search in this directory. */
        return 0;
    }
    return 1;
}

int fs_find_file(const struct fs *fs, const char *filename,
                 struct file_entry *fe)
{
    struct find_result res;
    struct file_entry root_fe;
    struct file_entry cur_fe;
    size_t pos, npos;

    if (!fs_file_entry(fs, 1, &root_fe)) {
        report_error("fs: find_file: "
                     "error finding SysDir at page 1");
        return FALSE;
    }

    pos = 0;
    cur_fe = root_fe;
    while (filename[pos]) {
        if (filename[pos] == '<') {
            cur_fe = root_fe;
            pos++;
            continue;
        }

        npos = pos + 1;
        while (filename[npos]) {
            if (filename[npos] == '<' || filename[npos] == '>')
                break;
            npos++;
        }

        res.filename = &filename[pos];
        res.flen = npos - pos;
        res.found = FALSE;

        if (res.flen >= FILENAME_LENGTH) return FALSE;

        if (!fs_scan_directory(fs, &cur_fe, &find_file_cb, &res)) {
            report_error("fs: find_file: could not scan directory");
            return FALSE;
        }

        if (!res.found) return FALSE;
        cur_fe = res.fe;

        if (filename[npos] == '>') {
            /* Checks if its a directory. */
            if (!(cur_fe.sn.word1 & SN_DIRECTORY)) {
                report_error("fs: find_file: not a valid directory");
                return FALSE;
            }
            npos++;
        }

        pos = npos;
    }

    *fe = cur_fe;
    return TRUE;
}


/* Auxiliary callback used by fs_scavenge_file().
 * The `arg` parameter is a pointer to find_result structure.
 */
static
int scavenge_file_cb(const struct fs *fs,
                     const struct file_entry *fe,
                     void *arg)
{
    struct scavenge_result *res;
    struct file_info finfo;

    res = (struct scavenge_result *) arg;
    if (!fs_file_info(fs, fe, &finfo)) {
        report_error("fs: scavenge_file: could not get file information");
        return -1;
    }

    if (strcmp(finfo.filename, res->filename) == 0) {
        res->fe = *fe;
        res->found++;
        /* Continue the search (to check if there exists only one
         * file with the given name).
         */
    }
    return 1;
}

int fs_scavenge_file(const struct fs *fs, const char *filename,
                     struct file_entry *fe)
{
    struct scavenge_result res;

    res.filename = filename;
    res.found = 0;
    if (!fs_scan_files(fs, &scavenge_file_cb, &res)) {
        report_error("fs: scavenge_file: could not scan filesystem");
        return FALSE;
    }

    if (res.found == 1) {
        *fe = res.fe;
        return TRUE;
    }

    return FALSE;
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

    if (!fs_open(fs, fe, &of, FALSE)) {
        report_error("fs: scan_directory: "
                     "could not open directory");
        return FALSE;
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, 2);
        if (of.error) goto error_read;

        if (nbytes == 0) break;
        if (nbytes != 2) goto error_short;

        w = read_word_bs(buffer, 0);
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

        de.fe.sn.word1 = read_word_bs(buffer, DIRECTORY_SN);
        de.fe.sn.word2 = read_word_bs(buffer, 2 + DIRECTORY_SN);
        de.fe.version = read_word_bs(buffer, DIRECTORY_VERSION);
        de.fe.blank = 0;
        de.fe.leader_vda = read_word_bs(buffer, DIRECTORY_LEADER_VDA);
        copy_name(de.filename, (const char *) &buffer[DIRECTORY_FILENAME]);

        if (de.type == DIR_ENTRY_VALID) {
            if (to_read <= DIRECTORY_FILENAME) {
                report_error("fs: scan_directory: "
                             "entry length too short: %lu",
                             to_read);
                return FALSE;
            }

            nbytes = (size_t) buffer[DIRECTORY_FILENAME];
            nbytes += DIRECTORY_FILENAME;
            if (nbytes > to_read) {
                report_error("fs: scan_directory: "
                             "string buffer overflow");
                return FALSE;
            }
        }

        ret = cb(fs, &de, arg);
        if (ret < 0) return FALSE;
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

/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter.
 * Returns TRUE on success.
 */
static
int real_to_virtual(const struct fs *fs, uint16_t rda, uint16_t *vda)
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

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter.
 * Returns TRUE on success.
 */
static
int virtual_to_real(const struct fs *fs, uint16_t vda, uint16_t *rda)
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

/* Copies the filename to `dst` and set the proper
 * NUL byte at the end of the string.
 */
static
void copy_name(char *dst, const char *src)
{
    uint8_t slen;

    slen = (uint8_t) src[0];
    if (slen >= FILENAME_LENGTH)
        slen = FILENAME_LENGTH - 1;

    if (slen == 0) {
        dst[0] = '\0';
        return;
    }

    memcpy(dst, &src[1], ((size_t) slen) - 1);
    dst[slen - 1] = '\0';
}

/* Reads a word (in big endian format).
 * The source data is given by `data`, and the offset where
 * the word is in `offset`.
 * Returns the word.
 */
static
uint16_t read_word_bs(const uint8_t *data, size_t offset)
{
    uint16_t w;
    w = (uint16_t) (data[offset + 1]);
    w |= (uint16_t) (data[offset] << 8);
    return w;
}

/* Obtains a time_t from the Alto filesystem.
 * The alto data is located at `offset` in `data`.
 * Returns the time.
 */
static
time_t read_alto_time(const uint8_t *data, size_t offset)
{
    time_t time;

    time = (int) read_word_bs(data, offset + 2);
    time += ((int) read_word_bs(data, offset)) << 16;

    time += 2117503696; /* magic value to convert to Unix epoch. */
    return time;
}
