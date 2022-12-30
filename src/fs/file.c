
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Functions. */

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

int fs_new_file(struct fs *fs, int directory,
                struct open_file *of)
{
    struct page *pg;
    uint16_t vda;

    if (!fs_find_free_page(fs, &vda)) {
        report_error("fs: create: "
                     "could not find free page");
        return FALSE;
    }

    pg = &fs->pages[vda];
    pg->label.prev_rda = 0;
    pg->label.next_rda = 0;
    pg->label.unused = 0;
    pg->label.nbytes = PAGE_DATA_SIZE;
    pg->label.file_pgnum = 0;
    pg->label.version = 1;
    pg->label.sn = fs->last_sn;
    if (directory) {
        pg->label.sn.word1 |= SN_DIRECTORY;
    }
    fs_increment_serial_number(fs);

    if (!fs_file_entry(fs, vda, &of->fe)) {
        report_error("fs: create: "
                     "could not get file entry");
        return FALSE;
    }
    of->pos.pgnum = 1;
    of->pos.pos = 0;
    of->pos.vda = of->fe.leader_vda;

    of->eof = FALSE;
    of->error = FALSE;
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
        if (!real_to_virtual(&fs->dg, rda, &vda)) {
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
        if (!virtual_to_real(&fs->dg, pg->page_vda,
                             &npg->label.prev_rda)) {
            of->error = TRUE;
            report_error("fs: write: could not convert virtual "
                         "to real disk address");
            break;
        }

        if (!virtual_to_real(&fs->dg, npg->page_vda,
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

int fs_read_leader_page(const struct fs *fs,
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

int fs_file_info(const struct fs *fs,
                 const struct file_entry *fe,
                 struct file_info *finfo)
{
    uint8_t data[PAGE_DATA_SIZE];

    if (!fs_read_leader_page(fs, fe, data)) {
        report_error("fs: file_info: "
                     "could not read leader page");
        return FALSE;
    }

    finfo->name_length = data[LD_OFF_NAME];
    read_name(&data[LD_OFF_NAME], finfo->name);
    finfo->created = read_alto_time(data, LD_OFF_CREATED);
    finfo->written = read_alto_time(data, LD_OFF_WRITTEN);
    finfo->read = read_alto_time(data, LD_OFF_READ);

    finfo->propbegin = data[LD_OFF_PROPBEGIN];
    finfo->proplen = data[LD_OFF_PROPLEN];
    finfo->consecutive = data[LD_OFF_CONSECUTIVE];
    finfo->change_sn = data[LD_OFF_CHANGESN];

    finfo->fe.sn.word1 = read_word_be(data, LD_OFF_DIRFPHINT);
    finfo->fe.sn.word2 = read_word_be(data, LD_OFF_DIRFPHINT + 2);
    finfo->fe.version = read_word_be(data, LD_OFF_DIRFPHINT + 4);
    finfo->fe.blank = read_word_be(data, LD_OFF_DIRFPHINT + 6);
    finfo->fe.leader_vda = read_word_be(data, LD_OFF_DIRFPHINT + 8);

    finfo->last_page.vda = read_word_be(data, LD_OFF_LASTPAGEHINT);
    finfo->last_page.pgnum = read_word_be(data, LD_OFF_LASTPAGEHINT + 2);
    finfo->last_page.pos = read_word_be(data, LD_OFF_LASTPAGEHINT + 4);
    return TRUE;
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
    write_geometry(buffer, DESCR_OFF_GEOMETRY, &fs->dg);
    write_word_be(buffer, DESCR_OFF_LAST_SN, fs->last_sn.word1);
    write_word_be(buffer, DESCR_OFF_LAST_SN + 2, fs->last_sn.word2);
    write_word_be(buffer, DESCR_OFF_DISKBT_SIZE, fs->bitmap_size);
    /* TODO: Use the correct versions kept. */
    write_word_be(buffer, DESCR_OFF_VERSIONS_KEPT, 0);
    write_word_be(buffer, DESCR_OFF_FREE_PAGES, fs->free_pages);

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

    if (!fs_read_leader_page(fs, fe, data)) {
        report_error("fs: update_leader_page: "
                     "could not read leader page");
        return FALSE;
    }

    if (!fs_file_length(fs, fe, &length, &end_of)) {
        report_error("fs: update_leader_page: "
                     "could not determine length");
        return FALSE;
    }

    write_word_be(data, LD_OFF_DIRFPHINT, fe->sn.word1);
    write_word_be(data, LD_OFF_DIRFPHINT + 2, fe->sn.word2);
    write_word_be(data, LD_OFF_DIRFPHINT + 4, fe->version);
    write_word_be(data, LD_OFF_DIRFPHINT + 6, 0); /* blank. */
    write_word_be(data, LD_OFF_DIRFPHINT + 8, fe->leader_vda);

    write_word_be(data, LD_OFF_LASTPAGEHINT, end_of.pos.vda);
    write_word_be(data, LD_OFF_LASTPAGEHINT + 2, end_of.pos.pgnum);
    write_word_be(data, LD_OFF_LASTPAGEHINT + 4, end_of.pos.pos);

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
