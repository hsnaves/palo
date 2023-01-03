
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by compress_cb(). */
struct compress_cb_arg {
    struct fs *fs;                /* A non-const reference to fs. */
    struct open_file of;          /* Open file for the directory. */
    int do_compress;              /* To actually do the compression. */
    size_t used_length;           /* Total used length. */
    size_t empty_length;          /* The total empty length. */
    int has_error;                /* If an error occurred. */
};

/* Functions. */

/* Auxiliary callback used by update_reference_counts().
 * The `arg` parameter is a pointer to fs struct (non-const).
 * structure.
 */
static
int update_ref_count_cb(const struct fs *fs,
                        const struct directory_entry *de,
                        void *arg)
{
    struct fs *fs_rw;
    int not_seen;

    fs_rw = (struct fs *) arg;

    if (de->type != DIR_ENTRY_VALID)
        return TRUE;

    not_seen = (fs_rw->ref_count[de->fe.leader_vda]++ == 0);

    /* Check if already went into this directory
     * to avoid infinite recursion.
     */
    if ((de->fe.sn.word1 & SN_DIRECTORY) && (not_seen)) {
        scan_directory(fs, &de->fe, &update_ref_count_cb, arg);
    }

    return TRUE;
}

void update_reference_counts(struct fs *fs)
{
    struct file_entry sysdir_fe;
    struct directory_entry sysdir_de;

    fs_get_sysdir(fs, &sysdir_fe);

    /* Fake directory entry containing the SysDir. */
    sysdir_de.fe = sysdir_fe;
    sysdir_de.type = DIR_ENTRY_VALID;
    strcpy(sysdir_de.name, "SysDir");
    sysdir_de.name_length = 1 + strlen(sysdir_de.name);
    update_directory_entry_length(&sysdir_de);

    memset(fs->ref_count, 0, fs->length * sizeof(uint16_t));
    update_ref_count_cb(fs, &sysdir_de, fs);
}

int fetch_directory_entry(const struct fs *fs,
                          struct open_file *of,
                          struct directory_entry *de)
{
    uint8_t buffer[64];
    size_t nbytes, byte_length;

    if (of->error < 0) return -1;
    if (of->eof) return 0;

    nbytes = fs_read(fs, of, buffer, 2);
    if (of->error < 0) return FALSE;
    if (nbytes == 0) return FALSE;
    if (nbytes != 2) goto error_fetch;

    /* Predecode the length of the directory_entry. */
    read_directory_entry(buffer, 0, de);

    byte_length = 2 * ((size_t) de->length);
    if (byte_length <= DIR_OFF_NAME) goto error_fetch;

    if (byte_length > sizeof(buffer)) {
        nbytes = fs_read(fs, of, &buffer[2], sizeof(buffer) - 2);
        if ((nbytes != sizeof(buffer) - 2) || (of->error < 0))
            goto error_fetch;

        byte_length -= sizeof(buffer);

        /* Discard the remaining data. */
        nbytes = fs_read(fs, of, NULL, byte_length);
        if ((nbytes != byte_length) || (of->error < 0))
            goto error_fetch;
    } else {
        nbytes = fs_read(fs, of, &buffer[2], byte_length - 2);
        if ((nbytes != byte_length - 2) || (of->error < 0))
            goto error_fetch;
    }

    read_directory_entry(buffer, 0, de);
    return TRUE;

error_fetch:
    if (of->error >= 0)
        of->error = ERROR_INVALID_DE;
    return FALSE;
}

/* Writes the contents of a directory_entry to the open_file `of`.
 * The directory_entry parameter is `de`. The parameter `extends` is passed
 * to fs_write() to indicate that the file can be extended.
 * Returns TRUE if the directory_entry was successfully written.
 */
static
int append_directory_entry(struct fs *fs,
                           struct open_file *of,
                           const struct directory_entry *de,
                           int extend)
{
    uint8_t buffer[64];
    size_t nbytes, byte_length;

    if ((of->error < 0) || of->eof) goto error_append;

    byte_length = 2 * ((size_t) de->length);
    if ((byte_length <= DIR_OFF_NAME) && (de->type == DIR_ENTRY_VALID)) {
        of->error = ERROR_INVALID_DE;
        return FALSE;
    }

    memset(buffer, 0, sizeof(buffer));
    write_directory_entry(buffer, 0, de);

    if (byte_length > sizeof(buffer)) {
        nbytes = fs_write(fs, of, buffer, sizeof(buffer), extend);
        if ((nbytes != sizeof(buffer)) || (of->error < 0))
            goto error_append;

        byte_length -= sizeof(buffer);

        /* Write NUL bytes as the remaining data. */
        nbytes = fs_write(fs, of, NULL, byte_length, extend);
        if ((nbytes != byte_length) || (of->error < 0))
            goto error_append;
    } else {
        nbytes = fs_write(fs, of, buffer, byte_length, extend);
        if ((nbytes != byte_length) || (of->error < 0))
            goto error_append;
    }

    return TRUE;

error_append:
    if (of->error >= 0)
        of->error = ERROR_DIR_FULL;
    return FALSE;
}

/* Appends the empty entries at the end of the directory.
 * The parameter `of` is the open_file of the currently open directory.
 * The `empty_length` specifies how many empty words are left.
 * Returns TRUE on success.
 */
static
int append_empty_entries(struct fs *fs,
                         struct open_file *of,
                         size_t empty_length)
{
    struct directory_entry de;

    memset(&de, 0, sizeof(struct directory_entry));
    de.type = DIR_ENTRY_MISSING;
    while (empty_length > 0) {
        if (empty_length >= 100) {
            de.length = 100;
        } else {
            de.length = empty_length;
        }
        empty_length -= (size_t) de.length;

        if (!append_directory_entry(fs, of, &de, FALSE)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Auxiliary function used by compress_directory().
 * The `arg` parameter is a pointer to compress_cb_arg structure.
 */
static
int compress_dir_cb(const struct fs *fs,
                    const struct directory_entry *de,
                    void *arg)
{
    struct compress_cb_arg *c_arg;

    UNUSED(fs);
    c_arg = (struct compress_cb_arg *) arg;

    if (de->type != DIR_ENTRY_VALID) {
        c_arg->empty_length += (size_t) de->length;
        return TRUE;
    }

    c_arg->used_length += (size_t) de->length;
    if (!c_arg->do_compress) return TRUE;

    if (!append_directory_entry(c_arg->fs, &c_arg->of, de, FALSE)) {
        report_error("fs: compress_directory: "
                     "%s", fs_error(c_arg->of.error));
        c_arg->has_error = TRUE;
        return FALSE;
    }
    return TRUE;
}

int compress_directory(struct fs *fs,
                       const struct file_entry *dir_fe,
                       int do_compress,
                       size_t *used_length,
                       size_t *empty_length)
{
    struct compress_cb_arg c_arg;

    c_arg.fs = fs;
    c_arg.do_compress = do_compress;
    c_arg.used_length = 0;
    c_arg.empty_length = 0;
    c_arg.has_error = FALSE;
    if (do_compress) {
        fs_get_of(fs, dir_fe, TRUE, FALSE, &c_arg.of);
        if (c_arg.of.error < 0) {
            report_error("fs: compress_directory: "
                         "%s", fs_error(c_arg.of.error));
            return FALSE;
        }
    }

    scan_directory(fs, dir_fe, &compress_dir_cb, &c_arg);
    *used_length = c_arg.used_length;
    *empty_length = c_arg.empty_length;

    if (c_arg.has_error) {
        report_error("fs: compress_directory: "
                     "could not compress");
        fs_close(fs, &c_arg.of);
        return FALSE;
    }
    if (!do_compress) return TRUE;

    if (c_arg.empty_length > 0) {
        if (!append_empty_entries(fs, &c_arg.of,
                                  c_arg.empty_length)) {

            report_error("fs: compress_directory: "
                         "%s", fs_error(c_arg.of.error));
            fs_close(fs, &c_arg.of);
            return FALSE;
        }
    }

    fs_close(fs, &c_arg.of);
    return TRUE;
}

int add_directory_entry(struct fs *fs,
                        const struct file_entry *dir_fe,
                        const struct directory_entry *de,
                        int do_add)
{
    size_t used_length, empty_length;
    struct open_file of;

    if (!compress_directory(fs, dir_fe, do_add,
                            &used_length, &empty_length)) {
        report_error("fs: add_directory_entry: "
                     "could not compress");
        return FALSE;
    }

    if (empty_length < de->length)
        return FALSE;

    if (!do_add)
        return TRUE;

    fs_get_of(fs, dir_fe, TRUE, FALSE, &of);
    fs_read(fs, &of, NULL, 2 * used_length);
    if (of.error < 0) goto error_add;

    if (!append_directory_entry(fs, &of, de, FALSE))
        goto error_add;

    empty_length -= de->length;
    if (empty_length == 0) return TRUE;

    if (!append_empty_entries(fs, &of, empty_length))
        goto error_add;

    fs_close(fs, &of);
    return TRUE;

error_add:
    /* This should never happen. */
    report_error("fs: add_directory_entry: "
                 "%s", fs_error(of.error));
    fs_close(fs, &of);
    return FALSE;
}
