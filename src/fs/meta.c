
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"


/* Functions. */

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

/* Auxiliary callback used by fs_file_info().
 * The `arg` parameter is a pointer to a file_info structure.
 */
static
int file_properties_cb(const struct fs *fs,
                       const struct file_entry *fe,
                       uint8_t type, uint8_t length,
                       const uint8_t *data, void *arg)
{
    struct file_info *finfo;

    UNUSED(fs);
    UNUSED(fe);
    finfo = (struct file_info *) arg;

    if (type == 1) {
        if (length != 5) {
            report_error("fs: file_info: "
                         "invalid property length");
            return -1;
        }

        read_geometry(data, 0, &finfo->dg);
        finfo->has_dg = TRUE;
    }
    return 1;
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
    read_name(data, LD_OFF_NAME, finfo->name);
    finfo->created = read_alto_time(data, LD_OFF_CREATED);
    finfo->written = read_alto_time(data, LD_OFF_WRITTEN);
    finfo->read = read_alto_time(data, LD_OFF_READ);

    finfo->propbegin = data[LD_OFF_PROPBEGIN];
    finfo->proplen = data[LD_OFF_PROPLEN];
    finfo->consecutive = data[LD_OFF_CONSECUTIVE];
    finfo->change_sn = data[LD_OFF_CHANGESN];

    finfo->has_dg = FALSE;
    if (!fs_scan_properties(fs, fe, &file_properties_cb, finfo)) {
        report_error("fs: file_info: "
                     "could not scan properties");
        return FALSE;
    }

    read_file_entry(data, LD_OFF_DIRFPHINT, &finfo->fe);
    read_file_position(data, LD_OFF_LASTPAGEHINT, &finfo->last_page);
    return TRUE;
}

/* Auxiliary function to write the leader page.
 * The contents of the raw leader page are in `data`, and
 * the file to write is indicated by `fe`.
 * Returns TRUE on success.
 */
int write_raw_leader_page(struct fs *fs,
                          const struct file_entry *fe,
                          uint8_t data[PAGE_DATA_SIZE])
{
    struct open_file of;
    size_t nbytes;
    if (!fs_open(fs, fe, &of)) {
        report_error("fs: write_raw_leader_page: "
                     "could not open file");
        return FALSE;
    }

    nbytes = fs_write(fs, &of, data, PAGE_DATA_SIZE, FALSE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: write_raw_leader_page: "
                     "could not write leader page");
        return FALSE;
    }

    return TRUE;
}

int fs_write_leader_page(struct fs *fs,
                         const struct file_entry *fe,
                         const struct file_info *finfo)
{
    uint8_t data[PAGE_DATA_SIZE];

    write_name(data, LD_OFF_NAME, finfo->name);
    data[LD_OFF_NAME] = finfo->name_length;

    write_alto_time(data, LD_OFF_CREATED, finfo->created);
    write_alto_time(data, LD_OFF_WRITTEN, finfo->written);
    write_alto_time(data, LD_OFF_READ, finfo->read);

    data[LD_OFF_PROPBEGIN] = finfo->propbegin;
    data[LD_OFF_PROPLEN] = finfo->proplen;
    data[LD_OFF_CONSECUTIVE] = finfo->consecutive;
    data[LD_OFF_CHANGESN] = finfo->change_sn;

    /* Clear the properties. */
    memset(&data[LD_OFF_PROPS], 0, LD_OFF_SPARE - LD_OFF_PROPS);
    if (finfo->has_dg) {
        /* TODO: Implement this. */
    }

    write_file_entry(data, LD_OFF_DIRFPHINT, &finfo->fe);
    write_file_position(data, LD_OFF_LASTPAGEHINT, &finfo->last_page);

    if (!write_raw_leader_page(fs, fe, data)) {
        report_error("fs: write_leader_page: "
                     "could not write leader page");
        return FALSE;
    }

    return TRUE;
}

int fs_update_leader_page(struct fs *fs, const struct file_entry *fe)
{
    struct open_file end_of;
    uint8_t data[PAGE_DATA_SIZE];
    size_t length;

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

    write_file_entry(data, LD_OFF_DIRFPHINT, fe);
    write_file_position(data, LD_OFF_LASTPAGEHINT, &end_of.pos);

    if (!write_raw_leader_page(fs, fe, data)) {
        report_error("fs: update_leader_page: "
                     "could not write leader page");
        return FALSE;
    }

    return TRUE;
}
