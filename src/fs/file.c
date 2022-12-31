
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
        report_error("fs: new_file: "
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
        report_error("fs: new_file: "
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
        report_error("fs: advance_page: error on file");
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
            report_error("fs: advance_page: could not convert real "
                         "to virtual disk address");
            return FALSE;
        }

        if (vda != 0) {
            /* If there is a valid next page. */
            if (vda >= fs->length) {
                of->error = TRUE;
                report_error("fs: advance_page: invalid VDA %u "
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
