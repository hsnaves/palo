
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Data structures and types. */

/* Auxiliary data structure used by compress_directory(). */
struct compress_cb_arg {
    struct fs *fs;                /* A non-const reference to fs. */
    struct open_file of;          /* Open file for the directory. */
    size_t used_length;           /* Total used length. */
    size_t empty_length;          /* The total empty length. */
    int has_error;                /* If an error occurred. */
};

/* Functions. */

int read_of_directory_entry(const struct fs *fs,
                            struct open_file *of,
                            struct directory_entry *de)
{
    uint8_t buffer[64];
    size_t nbytes, byte_length;

    if (of->error < 0) return -1;
    if (of->eof) return 0;

    nbytes = fs_read(fs, of, buffer, 2);
    if (nbytes == 0) return 0;
    if ((nbytes != 2) || (of->error < 0)) return -1;

    /* Predecode the length of the directory_entry. */
    read_directory_entry(buffer, 0, de);

    byte_length = 2 * ((size_t) de->length);
    if (byte_length <= DIR_OFF_NAME) return -1;

    if (byte_length > sizeof(buffer)) {
        nbytes = fs_read(fs, of, &buffer[2], sizeof(buffer) - 2);
        if ((nbytes != sizeof(buffer) - 2) || (of->error < 0))
            return -1;

        byte_length -= sizeof(buffer);

        /* Discard the remaining data. */
        nbytes = fs_read(fs, of, NULL, byte_length);
        if ((nbytes != byte_length) || (of->error < 0))
            return -1;
    } else {
        nbytes = fs_read(fs, of, &buffer[2], byte_length - 2);
        if ((nbytes != byte_length - 2) || (of->error < 0))
            return -1;
    }

    read_directory_entry(buffer, 0, de);
    return 1;
}

int write_of_directory_entry(struct fs *fs,
                             struct open_file *of,
                             const struct directory_entry *de,
                             int extend)
{
    uint8_t buffer[64];
    size_t nbytes, byte_length;

    if ((of->error < 0) || of->eof) return FALSE;

    byte_length = 2 * ((size_t) de->length);
    if (byte_length <= DIR_OFF_NAME) return FALSE;

    memset(buffer, 0, sizeof(buffer));
    write_directory_entry(buffer, 0, de);

    if (byte_length > sizeof(buffer)) {
        nbytes = fs_write(fs, of, buffer, sizeof(buffer), extend);
        if ((nbytes != sizeof(buffer)) || (of->error < 0))
            return FALSE;

        byte_length -= sizeof(buffer);

        /* Write NUL bytes as the remaining data. */
        nbytes = fs_write(fs, of, NULL, byte_length, extend);
        if ((nbytes != byte_length) || (of->error < 0))
            return FALSE;
    } else {
        nbytes = fs_write(fs, of, buffer, byte_length, extend);
        if ((nbytes != byte_length) || (of->error < 0))
            return FALSE;
    }

    return TRUE;
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

        if (!write_of_directory_entry(fs, of, &de, FALSE))
            return FALSE;
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
    if (!write_of_directory_entry(c_arg->fs, &c_arg->of, de, FALSE)) {
        c_arg->has_error = TRUE;
        return FALSE;
    }
    return TRUE;
}

void compress_directory(struct fs *fs,
                        const struct file_entry *dir_fe,
                        size_t *used_length, size_t *empty_length)
{
    struct compress_cb_arg c_arg;

    c_arg.fs = fs;
    c_arg.used_length = 0;
    c_arg.empty_length = 0;
    c_arg.has_error = FALSE;
    fs_get_of(fs, dir_fe, TRUE, &c_arg.of);
    if (c_arg.of.error < 0) {
        report_error("fs: compress_directory: "
                     "could not find directory");
        return;
    }

    scan_directory(fs, dir_fe, &compress_dir_cb, &c_arg);
    *used_length = c_arg.used_length;
    *empty_length = c_arg.empty_length;

    if (c_arg.has_error) {
        report_error("fs: compress_directory: "
                     "could not compress");
        return;
    }
    if (c_arg.empty_length == 0) return;

    if (!append_empty_entries(fs, &c_arg.of, c_arg.empty_length)) {
        report_error("fs: compress_directory: "
                     "could not append empty entries");
        return;
    }
}

int add_directory_entry(struct fs *fs,
                        const struct file_entry *dir_fe,
                        const struct directory_entry *de)
{
    size_t used_length, empty_length;
    struct open_file of;

    compress_directory(fs, dir_fe, &used_length, &empty_length);
    if (empty_length < de->length) return FALSE;

    fs_get_of(fs, dir_fe, TRUE, &of);
    fs_read(fs, &of, NULL, 2 * used_length);
    if (of.error < 0) return FALSE;

    if (!write_of_directory_entry(fs, &of, de, FALSE))
        return FALSE;

    empty_length -= de->length;
    if (empty_length == 0) return TRUE;

    if (!append_empty_entries(fs, &of, empty_length))
        return FALSE;

    return TRUE;
}
