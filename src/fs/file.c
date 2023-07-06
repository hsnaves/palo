
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
    fe->sn = pg->label.s.sn;
    fe->version = pg->label.s.version;
    fe->blank = 0;
    fe->leader_vda = leader_vda;
}

/* Creates a new file_entry in the filesystem.
 * If `directory` is non-zero, a directory is created.
 * In addition if it is bigger than 1, the disk geometry
 * is added to the file properties.
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
    pg->label.s.prev_rda = 0;
    pg->label.s.next_rda = 0;
    pg->label.s.unused = 0;
    /* Leader page is full. */
    pg->label.s.nbytes = fs->sector_bytes;
    pg->label.s.file_pgnum = 0;
    pg->label.s.version = 1;
    pg->label.s.sn = fs->last_sn;
    if (directory) {
        pg->label.s.sn.word1 |= SN_DIRECTORY;
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
    strncpy(finfo.name, base_name, NAME_LENGTH);
    finfo.name[NAME_LENGTH - 1] = '\0';

    if (directory > 1) {
        finfo.has_dg = TRUE;
        finfo.dg = fs->dg;
        finfo.props[0] = 1;
        finfo.props[1] = 5;
        write_geometry(finfo.props, 2, &finfo.dg);
    }

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
    rda = pg->label.s.next_rda;
    real_to_virtual(&fs->dg, rda, &vda);

    if (vda != 0) {
        /* If there is a valid next page. */
        of->pos.vda = vda;
        of->pos.pos = 0;
        of->pos.pgnum += 1;
    } else {
        /* Reached the end of file. */
        of->pos.pos = pg->label.s.nbytes;
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

/* Auxiliary function to resolve names (and validates the base_name).
 * It accepts the same parameters as fs_resolve_name(), but it also
 * has the `error` parameter which returns information about the validation
 * of the `base_name`.
 * Returns TRUE on success.
 */
static
int resolve_location(const struct fs *fs, const char *name, int *found,
                     struct file_entry *fe, struct file_entry *dir_fe,
                     const char **base_name, int *error)
{
    const char *_base_name;
    int _found, _error;
    int ret;

    _error = ERROR_NO_ERROR;
    if (!fs_resolve_name(fs, name, &_found, fe, dir_fe, &_base_name)) {
        _error = ERROR_FS_UNCHECKED;
        goto exit_resolve;
    }

    if (!_found) {
        ret = validate_name(_base_name);
        if (ret < 0) {
            _error = ERROR_INVALID_NAME;
            goto exit_resolve;
        }
        if (ret == 0) {
            _error = ERROR_DIR_NOT_FOUND;
            goto exit_resolve;
        }
    }

exit_resolve:
    if (found) {
        *found = _found;
    }
    if (base_name) {
        *base_name = _base_name;
    }
    if (error) {
        *error = _error;
    }
    return (_error >= 0);
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
    of->new_file = FALSE;
    memset(&of->dir_fe, 0, sizeof(struct file_entry));

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
    const char *base_name;
    int is_read_only;
    int found;

    if (!resolve_location(fs, name, &found, &fe, &dir_fe,
                          &base_name, &of->error))
        return FALSE;

    is_read_only = (strcmp(mode, "r") == 0);
    if (is_read_only || (strcmp(mode, "r+") == 0)) {
        if (!found) {
            of->error = ERROR_FILE_NOT_FOUND;
            return FALSE;
        }
        return fs_get_of(fs, &fe, TRUE, is_read_only, of);
    }

    if ((strcmp(mode, "w") != 0) && (strcmp(mode, "w+") != 0)) {
        of->error = ERROR_INVALID_MODE;
        return FALSE;
    }

    if (found) {
        fs_get_of(fs, &fe, TRUE, FALSE, of);
        if (strcmp(mode, "w") == 0) {
            fs_truncate(fs, of);
        }
        return (of->error >= 0);
    }

    if (!new_file_entry(fs, 0, base_name, &fe)) {
        of->error = ERROR_DISK_FULL;
        return FALSE;
    }

    fs_get_of(fs, &fe, TRUE, FALSE, of);
    of->new_file = TRUE;
    of->dir_fe = dir_fe;
    return (of->error >= 0);
}

int fs_close(struct fs *fs, struct open_file *of)
{
    struct directory_entry de;
    struct file_info finfo;

    if (!check_of(fs, of))
        return FALSE;

    if (of->modified) {
        update_leader_page(fs, &of->fe);
    }

    if (of->new_file) {
        fs_get_file_info(fs, &of->fe, &finfo, NULL);

        de.type = DIR_ENTRY_VALID;
        de.fe = of->fe;

        memset(de.name, 0, sizeof(de.name));
        de.name_length = finfo.name_length;
        strncpy(de.name, finfo.name, NAME_LENGTH);
        de.name[NAME_LENGTH - 1] = '\0';
        update_directory_entry_length(&de);

        if (!add_directory_entry(fs, &of->dir_fe, &de)) {
            of->error = ERROR_DIR_FULL;
            free_pages(fs, of->fe.leader_vda, TRUE);
            return FALSE;
        }
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

    if (!resolve_location(fs, name, &found, &fe, NULL,
                          NULL, &of->error))
        return FALSE;

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
        if (of->pos.pos < pg->label.s.nbytes) {
            nbytes = pg->label.s.nbytes - of->pos.pos;
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
        if (of->pos.pos < pg->label.s.nbytes) {
            nbytes = pg->label.s.nbytes - of->pos.pos;
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

        if (len == 0 && of->pos.pos < fs->sector_bytes)
            break;

        /* First check if there is a next page. */
        advance_page(fs, of);
        if (!of->eof) continue;

        /* Otherwise, reached the end of file. */
        if (!extend) break;

        of->eof = FALSE;

        /* Check if the last page can be extended. */
        if (pg->label.s.nbytes < fs->sector_bytes) {
            nbytes = fs->sector_bytes - pg->label.s.nbytes;
            if (nbytes > len) nbytes = len;
            pg->label.s.nbytes += nbytes;
            continue;
        }

        /* Otherwise, allocate a new page. */
        if (!allocate_page(fs, &vda, &pg->page_vda)) {
            of->error = ERROR_DISK_FULL;
            break;
        }

        npg = &fs->pages[vda];
        virtual_to_real(&fs->dg, pg->page_vda, &npg->label.s.prev_rda);
        virtual_to_real(&fs->dg, npg->page_vda, &pg->label.s.next_rda);

        nbytes = len;
        if (nbytes > fs->sector_bytes)
            nbytes = fs->sector_bytes;

        npg->label.s.next_rda = 0;
        npg->label.s.unused = pg->label.s.unused;
        npg->label.s.nbytes = nbytes;
        npg->label.s.file_pgnum = pg->label.s.file_pgnum + 1;
        npg->label.s.version = pg->label.s.version;
        npg->label.s.sn = pg->label.s.sn;
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

    if (of->pos.pos >= fs->sector_bytes) {
        advance_page(fs, of);
        of->eof = FALSE;
    }
    pg = &fs->pages[of->pos.vda];
    pg->label.s.nbytes = of->pos.pos;

    real_to_virtual(&fs->dg, pg->label.s.next_rda, &vda);
    pg->label.s.next_rda = 0;
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
    int found;
    int _error;

    if (!resolve_location(fs, name, &found, NULL, &dir_fe,
                          &base_name, &_error))
        goto exit_link;

    if (found) {
        _error = ERROR_ALREADY_EXIST;
        goto exit_link;
    }

    if (!check_file_entry(fs, fe, FALSE)) {
        _error = ERROR_INVALID_FE;
        goto exit_link;
    }

    de.type = DIR_ENTRY_VALID;
    de.fe = *fe;

    memset(de.name, 0, sizeof(de.name));
    de.name_length = strlen(base_name);
    strncpy(de.name, base_name, NAME_LENGTH);
    de.name[NAME_LENGTH - 1] = '\0';
    update_directory_entry_length(&de);

    if (!add_directory_entry(fs, &dir_fe, &de)) {
        _error = ERROR_DIR_FULL;
        goto exit_link;
    }

exit_link:
    if (error) {
        *error = _error;
    }
    return (_error >= 0);
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

    if (!resolve_location(fs, name, &found, &fe, &dir_fe,
                          &base_name, &_error))
        goto exit_unlink;

    if (!found) {
        _error = ERROR_FILE_NOT_FOUND;
        goto exit_unlink;
    }

    if (!remove_directory_entry(fs, &dir_fe, base_name)) {
        _error = ERROR_UNKNOWN;
        goto exit_unlink;
    }

    if (remove_underlying) {
        update_reference_counts(fs);
        if (fs->ref_count[fe.leader_vda] == 0) {
            free_pages(fs, fe.leader_vda, TRUE);
        }
    }

exit_unlink:
    if (error) {
        *error = _error;
    }
    return (_error >= 0);
}

int make_directory(struct fs *fs,
                   const char *name,
                   int is_sysdir,
                   int *error)
{
    struct open_file of;
    struct file_entry fe, dir_fe;
    const char *base_name;
    int found, directory;
    int _error;

    if (!resolve_location(fs, name, &found, NULL, &dir_fe,
                          &base_name, &_error))
        goto exit_mkdir;

    if (found) {
        _error = ERROR_ALREADY_EXIST;
        goto exit_mkdir;
    }

    directory = (is_sysdir) ? 2 : 1;
    if (!new_file_entry(fs, directory, base_name, &fe)) {
        _error = ERROR_DISK_FULL;
        goto exit_mkdir;
    }

    fs_get_of(fs, &fe, TRUE, FALSE, &of);
    of.new_file = (!is_sysdir);
    of.dir_fe = dir_fe;

    if (!append_empty_entries(fs, &of, 10000, TRUE)) {
        _error = of.error;
        fs_close(fs, &of);
        goto exit_mkdir;
    }

    fs_close(fs, &of);

exit_mkdir:
    if (error) {
        *error = _error;
    }
    return (_error >= 0);
}

int fs_mkdir(struct fs *fs,
             const char *name,
             int *error)
{
    return make_directory(fs, name, FALSE, error);
}

