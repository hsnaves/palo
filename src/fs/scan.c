
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by fs_resolve_name(). */
struct resolve_result {
    const char *name;             /* The name of the searched file. */
    size_t name_length;           /* Length of the name. */
    struct file_entry fe;         /* The file_entry of the file. */
    int found;                    /* If the file was found. */
};

/* Functions. */

void scan_properties(const struct fs *fs,
                     const struct file_entry *fe,
                     scan_property_cb cb, void *arg)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    const uint8_t *data;
    uint8_t type, length;
    size_t i, nbytes;
    int ret;

    read_leader_page(fs, fe, buffer);
    if (2 * buffer[LD_OFF_PROPBEGIN] != LD_OFF_PROPS)
        return;

    nbytes = 2 * buffer[LD_OFF_PROPLEN];
    if (nbytes > (LD_OFF_SPARE - LD_OFF_PROPS))
        /* ignore errors */
        return;

    data = &buffer[LD_OFF_PROPS];

    i = 0;
    while (i < nbytes) {
        type = data[i++];
        if (i == nbytes)
            /* ignore errors */
            return;

        length = data[i++];
        if (2 * length + i > nbytes)
            /* ignore errors */
            return;

        ret = cb(fs, fe, type, length, &data[i], arg);
        if (!ret) break;

        i += 2 * length;
    }
}

void scan_files(const struct fs *fs, scan_files_cb cb, void *arg)
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
        if (!ret) break;
    }
}

void scan_directory(const struct fs *fs, const struct file_entry *dir_fe,
                    scan_directory_cb cb, void *arg)
{
    struct directory_entry de;
    struct open_file of;
    int ret;

    fs_get_of(fs, dir_fe, TRUE, &of);
    while (TRUE) {
        ret = read_of_directory_entry(fs, &of, &de);

        if (ret == 0) break;
        if (ret < 0)
            /* Ignore errors. */
            return;

        ret = cb(fs, &de, arg);
        if (!ret) break;
    }
}

int fs_scan_directory(const struct fs *fs, const struct file_entry *dir_fe,
                      scan_directory_cb cb, void *arg)
{
    if (!fs->checked) {
        report_error("fs: scan_directory: filesystem not checked");
        return FALSE;
    }

    if (!check_file_entry(fs, dir_fe, FALSE)) {
        report_error("fs: scan_directory: invalid dir_fe");
        return FALSE;
    }

    if (!(dir_fe->sn.word1 & SN_DIRECTORY)) {
        report_error("fs: scan_directory: "
                     "dir_fe does not point to a directory");
        return FALSE;
    }

    scan_directory(fs, dir_fe, cb, arg);
    return TRUE;
}

/* Auxiliary callback used by resolve_name().
 * The `arg` parameter is a pointer to resolve_result structure.
 */
static
int resolve_name_cb(const struct fs *fs,
                    const struct directory_entry *de,
                    void *arg)
{
    struct resolve_result *res;

    UNUSED(fs);
    if (de->type == DIR_ENTRY_MISSING) {
        /* Skip missing entries (but do not stop). */
        return TRUE;
    }

    res = (struct resolve_result *) arg;
    if (strncmp(de->name, res->name, res->name_length) == 0) {
        res->fe = de->fe;
        res->found = TRUE;
        /* Stop the search in this directory. */
        return FALSE;
    }
    return TRUE;
}

int fs_resolve_name(const struct fs *fs, const char *name, int *found,
                    struct file_entry *fe, struct file_entry *dir_fe,
                    const char **suffix)
{
    struct resolve_result res;
    struct file_entry sysdir_fe;
    struct file_entry _fe, _dir_fe;
    size_t pos, npos;

    if (!fs->checked)
        return FALSE;

    fs_get_sysdir(fs, &sysdir_fe);

    pos = 0;
    _fe = _dir_fe = sysdir_fe;
    while (name[pos]) {
        if (name[pos] == '<') {
            _fe = _dir_fe = sysdir_fe;
            pos++;
            continue;
        }

        npos = pos + 1;
        while (name[npos]) {
            if (name[npos] == '<' || name[npos] == '>')
                break;
            npos++;
        }

        res.name = &name[pos];
        res.name_length = npos - pos;
        res.found = FALSE;

        scan_directory(fs, &_fe, &resolve_name_cb, &res);
        if (!res.found) {
            *found = FALSE;
            if (dir_fe) {
                *dir_fe = _fe;
            }
            if (suffix) {
                *suffix = res.name;
            }
            return TRUE;
        }

        _dir_fe = _fe;
        _fe = res.fe;

        if (name[npos] == '>') npos++;
        pos = npos;
    }

    *fe = _fe;
    if (dir_fe) {
        *dir_fe = _dir_fe;
    }

    *found = TRUE;
    return TRUE;
}
