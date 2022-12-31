
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by fs_find_file(). */
struct find_result {
    const char *name;             /* The name of the searched file. */
    size_t name_length;           /* Length of the name. */
    struct file_entry fe;         /* The file_entry of the file. */
    int found;                    /* If the file was found. */
};

/* Functions. */

int fs_scan_properties(const struct fs *fs,
                       const struct file_entry *fe,
                       scan_property_cb cb, void *arg)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    const uint8_t *data;
    uint8_t type, length;
    size_t i, nbytes;
    int ret;

    if (!fs_read_leader_page(fs, fe, buffer)) {
        report_error("fs: scan_properties: "
                     "could not read leader page");
        return FALSE;
    }

    if (buffer[LD_OFF_PROPBEGIN] == 0) return TRUE;

    if (2 * buffer[LD_OFF_PROPBEGIN] != LD_OFF_PROPS) {
        report_error("fs: scan_properties: "
                     "PROPBEGIN = %u != %u",
                     2 * buffer[LD_OFF_PROPBEGIN], LD_OFF_PROPS);
        return FALSE;
    }

    nbytes = 2 * buffer[LD_OFF_PROPLEN];
    if (nbytes > (LD_OFF_SPARE - LD_OFF_PROPS)) {
        report_error("fs: scan_properties: "
                     "invalid PROPLEN = %u",
                     buffer[LD_OFF_PROPLEN]);
        return FALSE;
    }

    data = &buffer[LD_OFF_PROPS];

    i = 0;
    while (i < nbytes) {
        type = data[i++];
        if (i == nbytes) {
            report_error("fs: scan_properties: "
                         "missing length");
            return FALSE;
        }
        length = data[i++];

        if (2 * length + i > nbytes) {
            report_error("fs: scan_properties: "
                         "overflow");
            return FALSE;
        }

        ret = cb(fs, fe, type, length, &data[i], arg);
        if (ret < 0) {
            report_error("fs: scan_properties: error while scanning");
            return FALSE;
        }
        if (ret == 0) break;

        i += 2 * length;
    }

    return TRUE;
}

int fs_scan_files(const struct fs *fs, scan_files_cb cb, void *arg)
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
        if (ret < 0) {
            report_error("fs: scan_files: error while scanning");
            return FALSE;
        }
        if (ret == 0) break;
    }

    return TRUE;
}

int fs_scan_directory(const struct fs *fs, const struct file_entry *fe,
                      scan_directory_cb cb, void *arg)
{
    struct directory_entry de;
    struct open_file of;
    uint16_t w;
    uint8_t buffer[128];
    size_t to_read, nbytes;
    int ret;

    if (!(fe->sn.word1 & SN_DIRECTORY)) {
        report_error("fs: scan_directory: "
                     "file_entry does not point to a directory");
        return FALSE;
    }

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: scan_directory: "
                     "could not open directory");
        return FALSE;
    }

    /* Skip the leader page. */
    nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: scan_directory: error while reading");
        return FALSE;
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, 2);
        if (of.error) goto error_read;

        if (nbytes == 0) break;
        if (nbytes != 2) goto error_short;

        w = read_word_be(buffer, 0);
        de.type = (w >> DIR_ENTRY_TYPE_SHIFT);

        de.length = (w & DIR_ENTRY_LEN_MASK);
        to_read = 2 * ((size_t) de.length);

        if (to_read > sizeof(buffer)) {
            nbytes = fs_read(fs, &of, &buffer[2], sizeof(buffer) - 2);
            if (of.error) goto error_read;
            if (nbytes != sizeof(buffer) - 2) goto error_short;
            to_read -= sizeof(buffer);

            /* Discard the remaining data. */
            nbytes = fs_read(fs, &of, NULL, to_read);
            if (of.error) goto error_read;
            if (nbytes != to_read) goto error_short;
        } else {
            nbytes = fs_read(fs, &of, &buffer[2], to_read - 2);
            if (of.error) goto error_read;
            if (nbytes != to_read - 2) goto error_short;
        }


        read_file_entry(buffer, DIR_OFF_FILE_ENTRY, &de.fe);

        de.name_length = buffer[DIR_OFF_NAME];
        read_name(buffer, DIR_OFF_NAME, de.name);

        ret = cb(fs, &de, arg);
        if (ret < 0) {
            report_error("fs: scan_directory: error while scanning");
            return FALSE;
        }
        if (ret == 0) break;
    }

    return TRUE;

error_read:
    report_error("fs: scan_directory: error while reading");
    return FALSE;

error_short:
    report_error("fs: scan_directory: entry too short");
    return FALSE;
}


/* Auxiliary callback used by fs_find_file().
 * The `arg` parameter is a pointer to find_result structure.
 */
static
int find_file_cb(const struct fs *fs,
                 const struct directory_entry *de,
                 void *arg)
{
    struct find_result *res;
    struct file_info finfo;

    if (de->type == DIR_ENTRY_MISSING) {
        /* Skip missing entries (but do not stop). */
        return 1;
    }

    res = (struct find_result *) arg;
    if (!fs_file_info(fs, &de->fe, &finfo)) {
        report_error("fs: find_file: could not get file information");
        return -1;
    }

    if (strncmp(finfo.name, res->name, res->name_length) == 0) {
        res->fe = de->fe;
        res->found = TRUE;
        /* Stop the search in this directory. */
        return 0;
    }
    return 1;
}

int fs_find_file(const struct fs *fs, const char *name,
                 struct file_entry *fe, struct file_entry *dir_fe)
{
    struct find_result res;
    struct file_entry root_fe;
    struct file_entry _fe, _dir_fe;
    size_t pos, npos;

    if (!fs_file_entry(fs, 1, &root_fe)) {
        report_error("fs: find_file: "
                     "error finding SysDir at page 1");
        return FALSE;
    }

    pos = 0;
    _fe = _dir_fe = root_fe;
    while (name[pos]) {
        if (name[pos] == '<') {
            _fe = _dir_fe = root_fe;
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

        if (res.name_length >= NAME_LENGTH) {
            report_error("fs: find_file: name too long");
            return FALSE;
        }

        if (!fs_scan_directory(fs, &_fe, &find_file_cb, &res)) {
            report_error("fs: find_file: could not scan directory");
            return FALSE;
        }

        if (!res.found) return FALSE;
        _dir_fe = _fe;
        _fe = res.fe;

        if (name[npos] == '>') npos++;
        pos = npos;
    }

    *fe = _fe;
    if (dir_fe) {
        *dir_fe = _dir_fe;
    }

    return TRUE;
}
