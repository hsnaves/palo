
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"


/* Functions. */

void read_leader_page(const struct fs *fs,
                      const struct file_entry *fe,
                      uint8_t data[PAGE_DATA_SIZE])
{
    struct open_file of;

    fs_get_of(fs, fe, FALSE, TRUE, &of);
    fs_read(fs, &of, data, PAGE_DATA_SIZE);
    fs_close_ro(fs, &of);
    if (of.error < 0) {
        /* This should not happen. */
        report_error("fs: read_leader_page: "
                     "error while reading: %s",
                     fs_error(of.error));
        memset(data, 0, PAGE_DATA_SIZE);
    }
}

/* Determines the file length.
 * The `fe` specifies the file. Optionally, the `end_of` returns a
 * pointer to the end of the file (if provided).
 * Returns the file length.
 */
static
size_t file_length(const struct fs *fs, const struct file_entry *fe,
                   struct open_file *end_of)
{
    struct open_file of;
    size_t l, nbytes;

    l = 0;
    fs_get_of(fs, fe, TRUE, TRUE, &of);
    if (of.error < 0) goto error_length;

    while (!of.eof) {
        nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
        l += nbytes;
    }

    if (of.error < 0) goto error_length;

    if (end_of) *end_of = of;
    fs_close_ro(fs, &of);
    return l;

error_length:
    if (end_of) *end_of = of;
    fs_close_ro(fs, &of);
    return l;
}

int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length, int *error)
{
    struct open_file end_of;

    *length = file_length(fs, fe, &end_of);
    if (error) {
        *error = end_of.error;
    }

    return (end_of.error >= 0);
}

/* Auxiliary callback used by file_info().
 * The `arg` parameter is a pointer to a file_info structure.
 */
static
int file_prop_cb(const struct fs *fs,
                 const struct file_entry *fe,
                 uint8_t type, uint8_t length,
                 const uint8_t *data, void *arg)
{
    struct file_info *finfo;

    UNUSED(fs);
    UNUSED(fe);
    finfo = (struct file_info *) arg;

    if (type == 1 && length == 5) {
        read_geometry(data, 0, &finfo->dg);
        finfo->has_dg = TRUE;
    }
    return 1;
}

int fs_get_file_info(const struct fs *fs,
                     const struct file_entry *fe,
                     struct file_info *finfo,
                     int *error)
{
    struct open_file of;
    uint8_t data[PAGE_DATA_SIZE];

    /* Test for errors. */
    fs_get_of(fs, fe, FALSE, TRUE, &of);
    fs_close_ro(fs, &of);
    if (error) {
        *error = of.error;
    }

    if (of.error < 0)
        return FALSE;

    read_leader_page(fs, fe, data);

    finfo->created = read_alto_time(data, LD_OFF_CREATED);
    finfo->written = read_alto_time(data, LD_OFF_WRITTEN);
    finfo->read = read_alto_time(data, LD_OFF_READ);

    finfo->name_length = data[LD_OFF_NAME];
    read_name(data, LD_OFF_NAME, finfo->name);

    memcpy(finfo->props, &data[LD_OFF_PROPS], sizeof(finfo->props));
    memcpy(finfo->spare, &data[LD_OFF_SPARE], sizeof(finfo->spare));

    finfo->propbegin = data[LD_OFF_PROPBEGIN];
    finfo->proplen = data[LD_OFF_PROPLEN];
    finfo->consecutive = data[LD_OFF_CONSECUTIVE];
    finfo->change_sn = data[LD_OFF_CHANGESN];

    finfo->has_dg = FALSE;
    scan_properties(fs, fe, &file_prop_cb, finfo);

    read_file_entry(data, LD_OFF_DIRFPHINT, &finfo->fe);
    read_file_position(data, LD_OFF_LASTPAGEHINT, &finfo->last_page);

    return TRUE;
}

/* Auxiliary function to write the leader page.
 * The contents of the raw leader page are in `data`, and
 * the file to write is indicated by `fe`.
 * Returns the error
 */
static
int write_raw_leader_page(struct fs *fs,
                          const struct file_entry *fe,
                          uint8_t data[PAGE_DATA_SIZE])
{
    struct open_file of;

    fs_get_of(fs, fe, FALSE, FALSE, &of);
    fs_write(fs, &of, data, PAGE_DATA_SIZE, FALSE);
    /* Close using fs_close_ro() to avoid updating the leader page. */
    of.read_only = TRUE; /* To avoid the error message. */
    fs_close_ro(fs, &of);
    return of.error;
}

int fs_set_file_info(struct fs *fs,
                     const struct file_entry *fe,
                     const struct file_info *finfo,
                     int *error)
{
    uint8_t data[PAGE_DATA_SIZE];
    int _error;

    write_alto_time(data, LD_OFF_CREATED, finfo->created);
    write_alto_time(data, LD_OFF_WRITTEN, finfo->written);
    write_alto_time(data, LD_OFF_READ, finfo->read);

    write_name(data, LD_OFF_NAME, finfo->name);
    data[LD_OFF_NAME] = finfo->name_length;

    data[LD_OFF_PROPBEGIN] = finfo->propbegin;
    data[LD_OFF_PROPLEN] = finfo->proplen;
    data[LD_OFF_CONSECUTIVE] = finfo->consecutive;
    data[LD_OFF_CHANGESN] = finfo->change_sn;

    memcpy(&data[LD_OFF_PROPS], finfo->props, sizeof(finfo->props));
    memcpy(&data[LD_OFF_SPARE], finfo->spare, sizeof(finfo->spare));

    write_file_entry(data, LD_OFF_DIRFPHINT, &finfo->fe);
    write_file_position(data, LD_OFF_LASTPAGEHINT, &finfo->last_page);

    _error = write_raw_leader_page(fs, fe, data);
    if (error) {
        *error = _error;
    }
    return (_error >= 0);
}

void update_leader_page(struct fs *fs, const struct file_entry *fe)
{
    struct open_file end_of;
    uint8_t data[PAGE_DATA_SIZE];
    int error;

    read_leader_page(fs, fe, data);
    file_length(fs, fe, &end_of);
    if (end_of.error < 0) {
        report_error("fs: update_leader_page: "
                     "could not determine length: %s",
                     fs_error(end_of.error));
        return;
    }

    write_file_position(data, LD_OFF_LASTPAGEHINT, &end_of.pos);
    error = write_raw_leader_page(fs, fe, data);
    if (error < 0) {
        /* This should never happen. */
        report_error("fs: update_leader_page: "
                     "could not write page: %s",
                     fs_error(error));
    }
}
