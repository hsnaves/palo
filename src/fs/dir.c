
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
};

/* Functions. */

/* Writes the contents of a directory_entry to the open_file `of`.
 * The directory_entry parameter is `de`. The parameter `extends` is passed
 * to _write() to indicate that the file can be extended.
 * Returns the number of bytes written.
 */
static
size_t append_directory_entry(struct fs *fs,
                              struct open_file *of,
                              const struct directory_entry *de,
                              int extend)
{
    uint8_t buffer[200];
    uint16_t w;

    if (de->type == DIR_ENTRY_MISSING) {
        memset(buffer, 0, sizeof(buffer));
    } else {
        write_file_entry(buffer, DIR_OFF_FILE_ENTRY, &de->fe);
        write_name(buffer, DIR_OFF_NAME, de->name);
        buffer[DIR_OFF_NAME] = de->name_length;
    }
    w = (de->type << DIR_ENTRY_TYPE_SHIFT);
    w += (de->length & DIR_ENTRY_LEN_MASK);
    write_word_be(buffer, 0, w);

    return _write(fs, of, buffer, 2 * de->length, extend);
}

/* Appends the empty entries at the end of the directory.
 * The parameter `of` is the open_file of the currently open directory.
 * The `empty_length` specifies how many empty words are left.
 */
static
void append_empty_entries(struct fs *fs,
                          struct open_file *of,
                          size_t empty_length)
{
    struct directory_entry de;

    de.type = DIR_ENTRY_MISSING;
    while (empty_length > 0) {
        if (empty_length >= 100) {
            de.length = 100;
        } else {
            de.length = empty_length;
        }
        empty_length -= (size_t) de.length;

        append_directory_entry(fs, of, &de, FALSE);
    }
}

/*Auxiliary function used by compress_directory().
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

    if (de->type == DIR_ENTRY_MISSING) {
        c_arg->empty_length += (size_t) de->length;
    } else {
        c_arg->used_length += (size_t) de->length;
        append_directory_entry(c_arg->fs, &c_arg->of, de, FALSE);
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
    get_of(fs, dir_fe, TRUE, &c_arg.of);

    scan_directory(fs, dir_fe, &compress_dir_cb, &c_arg);

    *used_length = c_arg.used_length;
    *empty_length = c_arg.empty_length;
    if (c_arg.empty_length > 0) {
        append_empty_entries(fs, &c_arg.of, c_arg.empty_length);
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

    get_of(fs, dir_fe, TRUE, &of);
    _read(fs, &of, NULL, 2 * used_length);

    append_directory_entry(fs, &of, de, FALSE);
    empty_length -= de->length;

    if (empty_length > 0) {
        append_empty_entries(fs, &of, empty_length);
    }
    return TRUE;
}
