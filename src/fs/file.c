
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Functions. */

void get_file_entry(const struct fs *fs, uint16_t leader_vda,
                    struct file_entry *fe)
{
    const struct page *pg;
    pg = &fs->pages[leader_vda];
    fe->sn = pg->label.sn;
    fe->version = pg->label.version;
    fe->blank = 0;
    fe->leader_vda = leader_vda;
}

void get_sysdir(const struct fs *fs, struct file_entry *sysdir_fe)
{
    get_file_entry(fs, 1, sysdir_fe);
}

int fs_get_sysdir(const struct fs *fs, struct file_entry *sysdir_fe)
{
    if (!fs->checked) {
        report_error("fs: sysdir: filesystem not checked");
        return FALSE;
    }

    get_sysdir(fs, sysdir_fe);
    return TRUE;
}

void get_of(const struct fs *fs,
            const struct file_entry *fe,
            int skip_leader,
            struct open_file *of)
{
    of->fe = *fe;
    of->pos.pgnum = 1;
    of->pos.pos = 0;
    of->pos.vda = of->fe.leader_vda;

    of->eof = FALSE;
    of->error = FALSE;

    if (skip_leader)
        advance_page(fs, of);
}

int fs_get_of(const struct fs *fs,
              const struct file_entry *fe,
              int skip_leader,
              struct open_file *of)
{
    if (!fs->checked) {
        report_error("fs: get_of: filesystem not checked");
        return FALSE;
    }

    if (!check_file_entry(fs, fe)) {
        report_error("fs: get_of: invalid file_entry fe");
        return FALSE;
    }

    get_of(fs, fe, skip_leader, of);
    return TRUE;
}

void new_file(struct fs *fs, uint16_t leader_vda, int directory,
              struct open_file *of)
{
    struct page *pg;

    pg = &fs->pages[leader_vda];
    pg->label.prev_rda = 0;
    pg->label.next_rda = 0;
    pg->label.unused = 0;
    /* Leader page is full. */
    pg->label.nbytes = PAGE_DATA_SIZE;
    pg->label.file_pgnum = 0;
    pg->label.version = 1;
    pg->label.sn = fs->last_sn;
    if (directory) {
        pg->label.sn.word1 |= SN_DIRECTORY;
    }
    increment_serial_number(fs);

    get_file_entry(fs, leader_vda, &of->fe);
    of->pos.pgnum = 0;
    of->pos.pos = 0;
    of->pos.vda = of->fe.leader_vda;

    of->eof = FALSE;
    of->error = FALSE;
}

void advance_page(const struct fs *fs, struct open_file *of)
{
    const struct page *pg;
    uint16_t vda, rda;

    /* Check for error or end of file. */
    if (of->error || of->eof) return;

    vda = of->pos.vda;
    pg = &fs->pages[vda];

    /* Go to the next page. */
    rda = pg->label.next_rda;
    real_to_virtual(&fs->dg, rda, &vda);

    if (vda != 0) {
        /* If there is a valid next page. */
        of->pos.vda = vda;
        of->pos.pos = 0;
        of->pos.pgnum += 1;
    } else {
        /* Reached the end of file. */
        of->eof = TRUE;
    }
}

size_t _read(const struct fs *fs, struct open_file *of,
             uint8_t *dst, size_t len)
{
    const struct page *pg;
    uint16_t vda, nbytes;
    size_t offset;

    if (of->error) return 0;

    offset = 0;
    while ((len > 0) && (!of->eof)) {
        vda = of->pos.vda;
        pg = &fs->pages[vda];

        /* If has not reached the end of the page. */
        if (of->pos.pos < pg->label.nbytes) {
            nbytes = pg->label.nbytes - of->pos.pos;
            if (nbytes > len) nbytes = len;

            if (dst) {
                memcpy(&dst[offset], &pg->data[of->pos.pos],
                       nbytes);
            }

            of->pos.pos += nbytes;
            offset += nbytes;
            len -= nbytes;
        }

        if (len == 0)
            break;

        advance_page(fs, of);
    }

    return offset;
}

size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len)
{
    if (!fs->checked) {
        report_error("fs: read: filesystem not checked");
        return 0;
    }

    if (!check_of(fs, of)) {
        report_error("fs: read: open_file not valid");
        return 0;
    }

    return _read(fs, of, dst, len);
}

size_t _write(struct fs *fs, struct open_file *of,
              const uint8_t *src, size_t len, int extend)
{
    struct page *pg, *npg;
    uint16_t vda, nbytes;
    size_t offset;

    if (of->error) return 0;

    offset = 0;
    while ((len > 0) && (!of->eof)) {
        vda = of->pos.vda;
        pg = &fs->pages[vda];

        /* If has not reached the end of the page. */
        if (of->pos.pos < pg->label.nbytes) {
            nbytes = pg->label.nbytes - of->pos.pos;
            if (nbytes > len) nbytes = len;

            if (src) {
                memcpy(&pg->data[of->pos.pos], &src[offset], nbytes);
            }

            of->pos.pos += nbytes;
            offset += nbytes;
            len -= nbytes;
        }

        if (of->pos.pos < pg->label.nbytes)
            break;

        /* First check if there is a next page. */
        advance_page(fs, of);
        if (!of->eof) continue;

        /* Otherwise, reached the end of file. */
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
        if (!find_free_page(fs, &vda)) {
            of->error = TRUE;
            report_error("fs: write: disk full");
            break;
        }

        npg = &fs->pages[vda];
        virtual_to_real(&fs->dg, pg->page_vda, &npg->label.prev_rda);
        virtual_to_real(&fs->dg, npg->page_vda, &pg->label.next_rda);

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

    return offset;
}

void trim(struct fs *fs, struct open_file *of)
{
    struct page *pg, *first_pg;

    if (of->error) return;

    first_pg = &fs->pages[of->pos.vda];
    if (of->pos.pos >= PAGE_DATA_SIZE) {
        advance_page(fs, of);
        first_pg = &fs->pages[of->pos.vda];
    }
    first_pg->label.nbytes = of->pos.pos;
    if (of->eof) return;

    while (TRUE) {
        advance_page(fs, of);
        if (of->eof) break;
        pg = &fs->pages[of->pos.vda];
        pg->label.version = VERSION_FREE;
    }

    first_pg->label.next_rda = 0;
}

int fs_open(const struct fs *fs,
            const char *name,
            const char *mode,
            struct open_file *of)
{
    struct file_entry fe, dir_fe;
    int found;

    if (!fs->checked) {
        report_error("fs: open: filesystem not checked");
        return FALSE;
    }

    resolve_name(fs, name, &found, &fe, &dir_fe);
    if (strcmp(mode, "r") == 0) {
        if (!found) {
            report_error("fs: open: file `%s` not found", name);
            return FALSE;
        }
        get_of(fs, &fe, TRUE, of);
    } else if (strcmp(mode, "w") == 0) {
        /* TODO: Implement this. */
    } else {
        report_error("fs: open: invalid mode `%s`", mode);
        return FALSE;
    }

    return TRUE;
}

int fs_close(const struct fs *fs,
             struct open_file *of)
{
    /* TODO: Implement this. */
    UNUSED(fs);
    of->eof = TRUE;
    return TRUE;
}
