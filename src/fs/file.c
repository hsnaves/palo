
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Functions. */

/* Converts the virtual disk address of the leader page `leader_vda` of a
 * file to a file_entry object `fe`.
 */
static
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

/* Creates a new file_entry in the filesystem.
 * The `leader_vda` parameter specifies the VDA of the leader page.
 * If `directory` is set to TRUE, a directory is created.
 * The created file_entry is stored in `fe`.
 */
static
void new_file_entry(struct fs *fs, uint16_t leader_vda,
                    int directory, struct file_entry *fe)
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

    get_file_entry(fs, leader_vda, fe);
}

/* Advances to the next page.
 * The open_file to advance is given in parameter `of`.
 */
static
void advance_page(const struct fs *fs, struct open_file *of)
{
    const struct page *pg;
    uint16_t vda, rda;

    /* Check for error or end of file. */
    if ((of->error < 0) || of->eof) return;

    vda = of->pos.vda;
    if (vda >= fs->length) {
        report_error("fs: advance_page: invalid VDA: %u", vda);
        return;
    }

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
        of->pos.pos = pg->label.nbytes;
        of->eof = TRUE;
    }
}

/* Validates a filename.
 * The parameter `name` contains the filename to validate.
 * Returns 1 if the name is valid, 0 if the name contains
 * "<" or ">" characters, or -1 if the name is too long.
 */
static
int validate_name(const char *name)
{
    size_t i;

    i = 0;
    while (TRUE) {
        if (name[i] == '\0') break;
        if (name[i] == '<' || name[i] == '>') {
            return 0;
        }
        i++;
    }
    if (i >= NAME_LENGTH - 1) {
        return -1;
    }
    return 1;
}

int fs_get_sysdir(const struct fs *fs, struct file_entry *sysdir_fe)
{
    if (!fs->checked)
        return FALSE;

    get_file_entry(fs, 1, sysdir_fe);
    return TRUE;
}

int fs_get_of(const struct fs *fs,
              const struct file_entry *fe,
              int skip_leader, int read_only,
              struct open_file *of)
{
    if (!fs->checked) {
        of->error = ERROR_FS_UNCHECKED;
        return FALSE;
    }

    if (!check_file_entry(fs, fe, FALSE)) {
        of->error = ERROR_INVALID_FE;
        return FALSE;
    }

    of->fe = *fe;
    of->pos.pgnum = 0;
    of->pos.pos = 0;
    of->pos.vda = of->fe.leader_vda;

    of->eof = FALSE;
    of->error = ERROR_NO_ERROR;
    of->read_only = read_only;
    of->modified = FALSE;

    if (skip_leader)
        advance_page(fs, of);

    return TRUE;
}

int fs_open(struct fs *fs,
            const char *name,
            const char *mode,
            struct open_file *of)
{
    struct file_entry fe, dir_fe;
    struct directory_entry de;
    struct file_info finfo;
    const char *base_name;
    uint16_t leader_vda;
    int found, ret;

    of->error = ERROR_UNKNOWN;
    if (!fs->checked) {
        of->error = ERROR_FS_UNCHECKED;;
        return FALSE;
    }

    if (!fs_resolve_name(fs, name, &found, &fe, &dir_fe, &base_name)) {
        /* Can only fail if the filesystem is unchecked. */
        of->error = ERROR_FS_UNCHECKED;
        return FALSE;
    }

    if ((strcmp(mode, "r") == 0) || (strcmp(mode, "r+") == 0)) {
        if (!found) {
            of->error = ERROR_FILE_NOT_FOUND;
            return FALSE;
        }
        return fs_get_of(fs, &fe, TRUE,
                         (strcmp(mode, "r") == 0),
                         of);
    }

    if ((strcmp(mode, "w") == 0) || (strcmp(mode, "w+") == 0)) {
        if (!found) {
            ret = validate_name(base_name);
            if (ret < 0) {
                of->error = ERROR_INVALID_NAME;
                return FALSE;
            }
            if (ret == 0) {
                of->error = ERROR_DIR_NOT_FOUND;
                return FALSE;
            }

            if (!allocate_page(fs, &leader_vda, NULL)) {
                of->error = ERROR_DISK_FULL;
                return FALSE;
            }

            new_file_entry(fs, leader_vda, FALSE, &fe);

            finfo.name_length = 1 + strlen(base_name);
            strncpy(finfo.name, base_name, NAME_LENGTH - 1);
            finfo.name[NAME_LENGTH - 1] = '\0';
            time(&finfo.created);
            finfo.written = finfo.created;
            finfo.read = finfo.created;
            finfo.propbegin = 0;
            finfo.proplen = 0;
            finfo.consecutive = FALSE;
            finfo.change_sn = FALSE;
            finfo.has_dg = FALSE;
            finfo.fe = fe;
            finfo.last_page.vda = leader_vda;
            finfo.last_page.pgnum = 0;
            finfo.last_page.pos = 0;
            write_leader_page(fs, &fe, &finfo);

            de.type = DIR_ENTRY_VALID;
            de.fe = fe;
            memcpy(de.name, finfo.name, NAME_LENGTH);
            de.name_length = finfo.name_length;
            update_directory_entry_length(&de);

            fs_get_of(fs, &fe, TRUE, FALSE, of);
            of->eof = FALSE;
            fs_write(fs, of, NULL, 0, TRUE);
            if (of->error < 0) {
                free_pages(fs, leader_vda, TRUE);
                return FALSE;
            }

            if (!add_directory_entry(fs, &dir_fe, &de, TRUE)) {
                of->error = ERROR_DIR_FULL;
                free_pages(fs, leader_vda, TRUE);
                return FALSE;
            }
        } else {
            fs_get_of(fs, &fe, TRUE, FALSE, of);
            if (strcmp(mode, "w") == 0) {
                fs_truncate(fs, of);
            }
        }
        if (of->error < 0) return FALSE;
        return TRUE;
    }

    of->error = ERROR_INVALID_MODE;
    return FALSE;
}

int fs_close(struct fs *fs, struct open_file *of)
{
    if (!check_of(fs, of))
        return FALSE;

    if (of->modified)
        update_leader_page(fs, &of->fe);

    of->eof = TRUE;
    return TRUE;
}

int fs_open_ro(const struct fs *fs,
               const char *name,
               struct open_file *of)
{
    struct file_entry fe;
    int found;

    of->error = ERROR_UNKNOWN;
    if (!fs->checked) {
        of->error = ERROR_FS_UNCHECKED;;
        return FALSE;
    }

    if (!fs_resolve_name(fs, name, &found, &fe, NULL, NULL)) {
        /* Can only fail if the filesystem is unchecked. */
        of->error = ERROR_FS_UNCHECKED;
        return FALSE;
    }

    if (!found) {
        of->error = ERROR_FILE_NOT_FOUND;
        return FALSE;
    }
    return fs_get_of(fs, &fe, TRUE, TRUE, of);
}

int fs_close_ro(const struct fs *fs, struct open_file *of)
{
    if (!check_of(fs, of))
        return FALSE;

    of->eof = TRUE;
    return TRUE;
}

size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len)
{
    const struct page *pg;
    uint16_t vda, nbytes;
    size_t offset;

    if (!check_of(fs, of))
        return 0;

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

size_t fs_write(struct fs *fs, struct open_file *of,
                const uint8_t *src, size_t len, int extend)
{
    struct page *pg, *npg;
    uint16_t vda, nbytes;
    size_t offset;

    if (!check_of(fs, of))
        return 0;

    if (of->read_only) {
        of->error = ERROR_READ_ONLY;
        return 0;
    }

    offset = 0;
    while (!of->eof) {
        of->modified = TRUE;

        vda = of->pos.vda;
        pg = &fs->pages[vda];

        /* If has not reached the end of the page. */
        if (of->pos.pos < pg->label.nbytes) {
            nbytes = pg->label.nbytes - of->pos.pos;
            if (nbytes > len) nbytes = len;

            if (src) {
                memcpy(&pg->data[of->pos.pos], &src[offset], nbytes);
            } else {
                memset(&pg->data[of->pos.pos], 0, nbytes);
            }

            of->pos.pos += nbytes;
            offset += nbytes;
            len -= nbytes;
        }

        if (len == 0 && of->pos.pos < PAGE_DATA_SIZE)
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
        if (!allocate_page(fs, &vda, &pg->page_vda)) {
            of->error = ERROR_DISK_FULL;
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

int fs_truncate(struct fs *fs, struct open_file *of)
{
    struct page *pg;
    uint16_t vda;

    if (!check_of(fs, of))
        return FALSE;

    if (of->read_only) {
        of->error = ERROR_READ_ONLY;
        return FALSE;
    }

    if (of->eof) return TRUE;
    of->modified = TRUE;

    if (of->pos.pos >= PAGE_DATA_SIZE) {
        advance_page(fs, of);
        of->eof = FALSE;
    }
    pg = &fs->pages[of->pos.vda];
    pg->label.nbytes = of->pos.pos;

    real_to_virtual(&fs->dg, pg->label.next_rda, &vda);
    pg->label.next_rda = 0;
    if (vda != 0) {
        free_pages(fs, vda, TRUE);
    }
    return TRUE;
}
