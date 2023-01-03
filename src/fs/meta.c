
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

    fs_get_of(fs, fe, FALSE, &of);
    fs_read(fs, &of, data, PAGE_DATA_SIZE);
    if (of.error < 0) {
        report_error("fs: read_leader_page: "
                     "error while reading leader page");
    }
}

size_t file_length(const struct fs *fs, const struct file_entry *fe,
                   struct open_file *end_of)
{
    struct open_file of;
    size_t l, nbytes;

    fs_get_of(fs, fe, TRUE, &of);

    l = 0;
    while (!of.eof) {
        nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
        l += nbytes;
    }

    if (of.error < 0) {
        report_error("fs: file_length: "
                     "error while reading file");
    }

    if (end_of) *end_of = of;
    return l;
}

int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length)
{
    if (!check_file_entry(fs, fe, FALSE))
        return FALSE;

    *length = file_length(fs, fe, NULL);
    return TRUE;
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

void file_info(const struct fs *fs,
               const struct file_entry *fe,
               struct file_info *finfo)
{
    uint8_t data[PAGE_DATA_SIZE];

    read_leader_page(fs, fe, data);

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
    scan_properties(fs, fe, &file_prop_cb, finfo);

    read_file_entry(data, LD_OFF_DIRFPHINT, &finfo->fe);
    read_file_position(data, LD_OFF_LASTPAGEHINT, &finfo->last_page);
}

int fs_file_info(const struct fs *fs,
                 const struct file_entry *fe,
                 struct file_info *finfo)
{
    if (!fs->checked)
        return FALSE;

    if (!check_file_entry(fs, fe, FALSE))
        return FALSE;

    file_info(fs, fe, finfo);
    return TRUE;
}

/* Auxiliary function to write the leader page.
 * The contents of the raw leader page are in `data`, and
 * the file to write is indicated by `fe`.
 * Returns TRUE on success.
 */
static
void write_raw_leader_page(struct fs *fs,
                           const struct file_entry *fe,
                           uint8_t data[PAGE_DATA_SIZE])
{
    struct open_file of;

    fs_get_of(fs, fe, FALSE, &of);
    fs_write(fs, &of, data, PAGE_DATA_SIZE, FALSE);

    if (of.error < 0) {
        report_error("fs: write_raw_leader_page: "
                     "error while writing file");
    }
}

void write_leader_page(struct fs *fs,
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
    if (finfo->has_dg && (2 * finfo->propbegin == LD_OFF_PROPS)
        && (finfo->proplen >= 5)) {

        data[LD_OFF_PROPS] = 1; /* type */
        data[LD_OFF_PROPS + 1] = 5; /* length */

        write_geometry(data, LD_OFF_PROPS + 2, &finfo->dg);
    }

    write_file_entry(data, LD_OFF_DIRFPHINT, &finfo->fe);
    write_file_position(data, LD_OFF_LASTPAGEHINT, &finfo->last_page);

    write_raw_leader_page(fs, fe, data);
}

void update_leader_page(struct fs *fs, const struct file_entry *fe)
{
    struct open_file end_of;
    uint8_t data[PAGE_DATA_SIZE];

    read_leader_page(fs, fe, data);
    file_length(fs, fe, &end_of);

    write_file_entry(data, LD_OFF_DIRFPHINT, fe);
    write_file_position(data, LD_OFF_LASTPAGEHINT, &end_of.pos);

    write_raw_leader_page(fs, fe, data);
}
