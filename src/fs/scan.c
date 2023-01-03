
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

    fs_get_of(fs, dir_fe, TRUE, TRUE, &of);
    while (TRUE) {
        if (!fetch_directory_entry(fs, &of, &de))
            /* Ignore errors. */
            break;

        if (!cb(fs, &de, arg))
            break;
    }
}

int fs_scan_directory(const struct fs *fs, const struct file_entry *dir_fe,
                      scan_directory_cb cb, void *arg, int *error)
{
    struct open_file of;

    fs_get_of(fs, dir_fe, TRUE, TRUE, &of);
    if (of.error >= 0) {
        if (!(dir_fe->sn.word1 & SN_DIRECTORY)) {
            of.error = ERROR_NOT_DIRECTORY;
        }
    }

    if (error) {
        *error = of.error;
    }

    if (of.error < 0) {
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
    if (de->type != DIR_ENTRY_VALID) {
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
                    const char **base_name)
{
    struct resolve_result res;
    struct file_entry sysdir_fe;
    struct file_entry _fe, _dir_fe;
    const char *_base_name;
    size_t pos, npos;

    if (!fs->checked)
        return FALSE;

    fs_get_sysdir(fs, &sysdir_fe);

    pos = 0;
    _fe = sysdir_fe;
    _base_name = name;
    while (name[pos]) {
        if (name[pos] == '<') {
            _fe = sysdir_fe;
            pos++;
            continue;
        }

        npos = pos + 1;
        while (name[npos]) {
            if (name[npos] == '<' || name[npos] == '>')
                break;
            npos++;
        }

        _dir_fe = _fe;
        _base_name = &name[pos];

        res.name = _base_name;
        res.name_length = npos - pos;
        res.found = FALSE;

        scan_directory(fs, &_dir_fe, &resolve_name_cb, &res);
        if (!res.found) {
            *found = FALSE;
            if (dir_fe) {
                *dir_fe = _dir_fe;
            }
            if (base_name) {
                *base_name = _base_name;
            }
            return TRUE;
        }

        _fe = res.fe;
        if (name[npos] == '>') npos++;
        pos = npos;
    }

    *fe = _fe;
    if (dir_fe) {
        *dir_fe = _dir_fe;
    }
    if (base_name) {
        *base_name = _base_name;
    }

    *found = TRUE;
    return TRUE;
}
