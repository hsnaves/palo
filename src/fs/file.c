
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
 * If `directory` is set to TRUE, a directory is created.
 * The `base_name` specifies the name of the file.
 * The created file_entry is stored in `fe`.
 * Returns TRUE on success.
 */
static
int new_file_entry(struct fs *fs,
                   int directory,
                   const char *base_name,
                   struct file_entry *fe)
{
    struct page *pg;
    struct open_file of;
    struct file_info finfo;
    uint16_t leader_vda;
    int error;

    if (!allocate_page(fs, &leader_vda, NULL))
        return FALSE;

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
    get_file_entry(fs, leader_vda, fe);

    /* Add one more page of data. */
    fs_get_of(fs, fe, TRUE, FALSE, &of);
    of.eof = FALSE;
    /* Write zero bytes to add one more page. */
    fs_write(fs, &of, NULL, 0, TRUE);
    /* Call fs_close_ro() to not write the leader page. */
    of.read_only = TRUE; /* To avoid the error message. */
    fs_close_ro(fs, &of);

    if (of.error < 0) {
        free_pages(fs, leader_vda, TRUE);
        return FALSE;
    }

    increment_serial_number(fs);
    memset(&finfo, 0, sizeof(finfo));

    time(&finfo.created);
    finfo.written = finfo.created;
    finfo.read = finfo.created;

    memset(finfo.name, 0, sizeof(finfo.name));
    finfo.name_length = strlen(base_name);
    strncpy(finfo.name, base_name, NAME_LENGTH - 1);

    finfo.propbegin = LD_OFF_PROPS / 2;
    finfo.proplen = (LD_OFF_SPARE - LD_OFF_PROPS) / 2;
    finfo.consecutive = FALSE;
    finfo.change_sn = FALSE;
    finfo.has_dg = FALSE;
    finfo.fe = *fe;
    finfo.last_page.vda = leader_vda;
    finfo.last_page.pgnum = 0;
    finfo.last_page.pos = 0;

    if (!fs_set_file_info(fs, fe, &finfo, &error)) {
        /* This should not happen. */
        report_error("fs: new_file_entry: "
                     "could not write leader page: %s",
                     fs_error(error));
        free_pages(fs, leader_vda, TRUE);
        return FALSE;
    }

    return TRUE;
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
    if ((!fs->checked) || (fs->length <= 1))
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
    const char *base_name;
    int found, ret, is_new_mode;
    int is_directory;

    of->error = ERROR_UNKNOWN;
    if (!fs->checked) {
        of->error = ERROR_FS_UNCHECKED;
        return FALSE;
    }

    is_new_mode = (strcmp(mode, "n") == 0)
        || (strcmp(mode, "nd") == 0);
    is_directory = (strcmp(mode, "wd") == 0)
        || (strcmp(mode, "nd") == 0);

    if (is_new_mode) {
        found = FALSE;
        base_name = name;
    } else {
        if (!fs_resolve_name(fs, name, &found,
                             &fe, &dir_fe, &base_name)) {
            /* Can only fail if the filesystem is unchecked. */
            of->error = ERROR_FS_UNCHECKED;
            return FALSE;
        }
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

    if ((strcmp(mode, "w") != 0)
        && (strcmp(mode, "w+") != 0)
        && (!is_new_mode) && (!is_directory)) {

        of->error = ERROR_INVALID_MODE;
        return FALSE;
    }

    if (found) {
        if (is_directory) {
            of->error = ERROR_ALREADY_EXIST;
            return FALSE;
        }

        fs_get_of(fs, &fe, TRUE, FALSE, of);
        if (strcmp(mode, "w") == 0) {
            fs_truncate(fs, of);
        }
        return (of->error >= 0);
    }

    ret = validate_name(base_name);
    if (is_new_mode) {
        if (ret <= 0) {
            of->error = ERROR_INVALID_NAME;
            return FALSE;
        }
    } else {
        if (ret < 0) {
            of->error = ERROR_INVALID_NAME;
            return FALSE;
        }
        if (ret == 0) {
            of->error = ERROR_DIR_NOT_FOUND;
            return FALSE;
        }
    }

    if (!new_file_entry(fs, is_directory, base_name, &fe)) {
        of->error = ERROR_DISK_FULL;
        return FALSE;
    }

    if (!is_new_mode) {
        de.type = DIR_ENTRY_VALID;
        de.fe = fe;

        memset(de.name, 0, sizeof(de.name));
        de.name_length = strlen(base_name);
        strncpy(de.name, base_name, NAME_LENGTH - 1);
        update_directory_entry_length(&de);

        if (!add_directory_entry(fs, &dir_fe, &de, TRUE)) {
            of->error = ERROR_DIR_FULL;
            free_pages(fs, fe.leader_vda, TRUE);
            return FALSE;
        }
    }

    fs_get_of(fs, &fe, TRUE, FALSE, of);
    return (of->error >= 0);
}

int fs_close(struct fs *fs, struct open_file *of)
{
    if (!check_of(fs, of))
        return FALSE;

    if (of->modified) {
        update_leader_page(fs, &of->fe);
    }

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
        of->error = ERROR_FS_UNCHECKED;
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

    if (!of->read_only) {
        report_error("fs: close_ro: "
                     "open_file not open in read_only mode");
    }

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
        npg->label.unused = pg->label.unused;
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

int fs_link(struct fs *fs,
            const char *name,
            const struct file_entry *fe,
            int *error)
{
    struct file_entry dir_fe;
    struct directory_entry de;
    const char *base_name;
    int found, ret;
    int _error;

    _error = ERROR_UNKNOWN;
    if (!fs->checked) {
        _error = ERROR_FS_UNCHECKED;
        goto error_link;
    }

    if (!check_file_entry(fs, fe, FALSE)) {
        _error = ERROR_INVALID_FE;
        goto error_link;
    }

    if (!fs_resolve_name(fs, name, &found, NULL, &dir_fe, &base_name)) {
        /* Can only fail if the filesystem is unchecked. */
        _error = ERROR_FS_UNCHECKED;
        goto error_link;
    }

    if (found) {
        _error = ERROR_ALREADY_EXIST;
        goto error_link;
    }

    ret = validate_name(base_name);
    if (ret < 0) {
        _error = ERROR_INVALID_NAME;
        goto error_link;
    }
    if (ret == 0) {
        _error = ERROR_DIR_NOT_FOUND;
        goto error_link;
    }

    de.type = DIR_ENTRY_VALID;
    de.fe = *fe;

    memset(de.name, 0, sizeof(de.name));
    de.name_length = strlen(base_name);
    strncpy(de.name, base_name, NAME_LENGTH - 1);
    update_directory_entry_length(&de);

    if (!add_directory_entry(fs, &dir_fe, &de, TRUE)) {
        _error = ERROR_DIR_FULL;
        goto error_link;
    }

    if (error) {
        *error = ERROR_NO_ERROR;
    }
    return TRUE;

error_link:
    if (error) {
        *error = _error;
    }
    return FALSE;
}

int fs_unlink(struct fs *fs,
              const char *name,
              int remove_underlying,
              int *error)
{
    struct file_entry fe, dir_fe;
    const char *base_name;
    int found;
    int _error;

    _error = ERROR_UNKNOWN;
    if (!fs->checked) {
        _error = ERROR_FS_UNCHECKED;
        goto error_unlink;
    }

    if (!fs_resolve_name(fs, name, &found, &fe, &dir_fe, &base_name)) {
        /* Can only fail if the filesystem is unchecked. */
        _error = ERROR_FS_UNCHECKED;
        goto error_unlink;
    }

    if (!found) {
        _error = ERROR_FILE_NOT_FOUND;
        goto error_unlink;
    }

    if (!remove_directory_entry(fs, &dir_fe, base_name, TRUE)) {
        _error = ERROR_UNKNOWN;
        goto error_unlink;
    }

    if (remove_underlying) {
        update_reference_counts(fs);

        if (fs->ref_count[fe.leader_vda] == 0) {
            free_pages(fs, fe.leader_vda, TRUE);
        }
    }

    if (error) {
        *error = ERROR_NO_ERROR;
    }
    return TRUE;

error_unlink:
    if (error) {
        *error = _error;
    }
    return FALSE;
}

int fs_mkdir(struct fs *fs,
             const char *name,
             int is_sysdir,
             int *error)
{
    struct open_file of;
    const char *mode;

    mode = (is_sysdir) ? "nd" : "wd";
    if (!fs_open(fs, name, mode, &of)) {
        if (error) {
            *error = of.error;
        }
        return FALSE;
    }

    if (!append_empty_entries(fs, &of, 10000, TRUE)) {
        goto error_mkdir;
    }

    fs_close(fs, &of);
    return TRUE;

error_mkdir:
    if (error) {
        *error = of.error;
    }
    fs_close(fs, &of);
    if (is_sysdir) {
        free_pages(fs, of.fe.leader_vda, TRUE);
    } else {
        fs_unlink(fs, name, TRUE, &of.error);
        if (of.error < 0) {
            report_error("fs: mkdir: "
                         "could not unlink directory: %s",
                         fs_error(of.error));
        }
    }
    return FALSE;
}

