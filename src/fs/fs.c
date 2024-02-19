
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Constants. */
#define DISK_PARAMS_REPLY                  3
#define DISK_PAGE_REPLY                    6
#define END_OF_TRANSFER                    7
#define DIABLO_DISK_TYPE                  10

/* Functions. */

void fs_initvar(struct fs *fs)
{
    fs->pages = NULL;
    fs->ref_count = NULL;
    fs->bitmap = NULL;
    fs->data = NULL;
}

void fs_destroy(struct fs *fs)
{
    if (fs->pages) free((void *) fs->pages);
    fs->pages = NULL;

    if (fs->ref_count) free((void *) fs->ref_count);
    fs->ref_count = NULL;

    if (fs->bitmap) free((void *) fs->bitmap);
    fs->bitmap = NULL;

    if (fs->data) free((void *) fs->data);
    fs->data = NULL;
}

int fs_create(struct fs *fs, struct geometry dg)
{
    size_t offset, size;
    uint16_t i;

    fs_initvar(fs);
    if (unlikely(dg.num_disks > 2
                 || dg.num_heads > 2
                 || dg.num_sectors > 15
                 || dg.num_cylinders >= 512
                 || dg.sector_words > 1024)) {
        report_error("fs: create: invalid disk geometry");
        return FALSE;
    }

    fs->dg = dg;

    fs->disk_length = dg.num_cylinders * dg.num_heads * dg.num_sectors;
    fs->length = dg.num_disks * fs->disk_length;
    fs->sector_bytes = dg.sector_words * sizeof(uint16_t);
    fs->bitmap_size = (fs->length >> 4);
    if (fs->length & 0xF) fs->bitmap_size++;

    size = ((size_t) fs->length) * sizeof(struct page);
    fs->pages = (struct page *) malloc(size);

    size = ((size_t) fs->length) * sizeof(uint16_t);
    fs->ref_count = (uint16_t *) malloc(size);

    size = ((size_t) fs->bitmap_size) * sizeof(uint16_t);
    fs->bitmap = (uint16_t *) malloc(size);

    size = ((size_t) fs->length) * fs->sector_bytes;
    fs->data = (uint8_t *) malloc(size);

    if (unlikely(!fs->pages || !fs->ref_count
                 || !fs->bitmap || !fs->data)) {
        report_error("fs: create: memory exhausted");
        fs_destroy(fs);
        return FALSE;
    }

    for (i = 0; i < fs->length; i++) {
        struct page *pg;
        pg = &fs->pages[i];

        offset = ((size_t) i) * fs->sector_bytes;
        pg->data = &fs->data[offset];
    }

    fs->free_pages = 0xFFFF;
    fs->last_sn.word1 = 0;
    fs->last_sn.word2 = 0;
    fs->checked = FALSE;

    return TRUE;
}

const char *fs_error(int error)
{
    static const char *errors[] = {
        [-ERROR_NO_ERROR]       = "no error",
        [-ERROR_UNKNOWN]        = "unknown error",
        [-ERROR_FS_UNCHECKED]   = "filesystem unchecked",
        [-ERROR_INVALID_OF]     = "invalid open_file",
        [-ERROR_INVALID_FE]     = "invalid file_entry",
        [-ERROR_INVALID_DE]     = "invalid directory_entry",
        [-ERROR_DISK_FULL]      = "disk full",
        [-ERROR_DIR_FULL]       = "directory full",
        [-ERROR_FILE_NOT_FOUND] = "file not found",
        [-ERROR_DIR_NOT_FOUND]  = "directory not found",
        [-ERROR_INVALID_NAME]   = "invalid name",
        [-ERROR_INVALID_MODE]   = "invalid mode",
        [-ERROR_READ_ONLY]      = "file in read-only mode",
        [-ERROR_NOT_DIRECTORY]  = "not a directory",
        [-ERROR_ALREADY_EXIST]  = "name already exist",
    };
    if (error > 0) error = 0;
    if (error <= ERROR_END) error = ERROR_UNKNOWN;

    return errors[-error];
}

/* Loads an AAR disk image.
 * The file to be read is given in the parameter `filename`.
 * This will populate the disk number `disk_num`.
 * Returns TRUE on success.
 */
static
int fs_load_image_aar(struct fs *fs, const char *filename,
                      uint16_t disk_num)
{
    FILE *fp;
    struct page *pg;
    uint16_t i, j, vda, base_vda;
    uint16_t header_len, label_len;
    uint16_t w;
    int c;

    fp = fopen(filename, "rb");
    if (!fp) {
        report_error("fs: load_image_aar: could not open `%s`",
                     filename);
        return FALSE;
    }

    fs->checked = FALSE;
    base_vda = disk_num * fs->disk_length;
    header_len = sizeof(pg->header) / sizeof(uint16_t);
    label_len = sizeof(pg->label) / sizeof(uint16_t);

    for (i = 0; i < fs->disk_length; i++) {
        vda = base_vda + i;
        pg = &fs->pages[vda];

        c = fgetc(fp);
        if (c == EOF) goto error_eof;

        c = fgetc(fp);
        if (c == EOF) goto error_eof;

        /* Discard the first word and use the loop index instead. */
        pg->page_vda = vda;

        for (j = 0; j < header_len; j++) {
            /* Process data in little-endian format. */
            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w |= (uint16_t) ((c & 0xFF) << 8);

            pg->header[j] = w;
        }

        for (j = 0; j < label_len; j++) {
            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w |= (uint16_t) ((c & 0xFF) << 8);

            pg->label.r[j] = w;
        }

        for (j = 0; j < fs->sector_bytes; j++) {
            c = fgetc(fp);
            if (c == EOF) goto error_eof;

            /* Byte swap the data here. */
            pg->data[j ^ 1] = (uint8_t) c;
        }
    }

    c = fgetc(fp);
    if (c != EOF) {
        report_error("fs: load_image_aar: "
                     "file `%s` longer than expected", filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("fs: load_image_aar: "
                 "premature end of file in `%s`", filename);
    fclose(fp);
    return FALSE;
}

/* Loads an BFS disk image.
 * The file to be read is given in the parameter `filename`.
 * This will populate the disk number `disk_num`.
 * Returns TRUE on success.
 */
static
int fs_load_image_bfs(struct fs *fs, const char *filename,
                      uint16_t disk_num)
{
    FILE *fp;
    struct page *pg;
    uint16_t base_vda, end_vda, vda;
    uint16_t header_len, label_len;
    uint16_t cmd, len;
    uint16_t params_reply[7], buffer[2];
    uint16_t v, pos, i;
    int c;

    fp = fopen(filename, "rb");
    if (!fp) {
        report_error("fs: load_image_bfs: could not open `%s`",
                     filename);
        return FALSE;
    }

    fs->checked = FALSE;
    base_vda = disk_num * fs->disk_length;
    end_vda = base_vda + fs->disk_length;
    header_len = sizeof(pg->header) / sizeof(uint16_t);
    label_len = sizeof(pg->label) / sizeof(uint16_t);

    params_reply[0] = 7; /* length of the command. */
    params_reply[1] = DISK_PARAMS_REPLY;
    params_reply[2] = DIABLO_DISK_TYPE;
    params_reply[3] = fs->dg.num_cylinders;
    params_reply[4] = fs->dg.num_heads;
    params_reply[5] = fs->dg.num_sectors;
    params_reply[6] = 1; /* number of disks. */

    cmd = 0;
    pos = len = 0;
    pg = NULL;
    while (TRUE) {
        c = fgetc(fp);
        if (c == EOF) goto incomplete_file;
        v = (uint16_t) c;

        c = fgetc(fp);
        if (c == EOF) goto incomplete_file;

        v <<= 8;
        v += (uint16_t) c;
        if (pos >= len) {
            pos = 0;
            len = v;
        } else if (pos == 1) {
            cmd = v;
            if (cmd == END_OF_TRANSFER) {
                c = fgetc(fp);
                if (c != EOF) {
                    report_error("fs: load_image_bfs: extra data at end "
                                 "of `%s`", filename);
                    fclose(fp);
                    return FALSE;
                }
                break;
            }
        } else {
            if (cmd == DISK_PARAMS_REPLY) {
                if (len != params_reply[0]) {
                    report_error("fs: load_image_bfs: invalid command length "
                                 " %u for DiskParamsReply in file `%s`",
                                 len, filename);
                    fclose(fp);
                    return FALSE;
                }

                if (params_reply[pos] != v) {
                    report_error("fs: load_image_bfs: discrepancy in disk "
                                 "parameters at position %u in file `%s`",
                                 pos, filename);
                    fclose(fp);
                    return FALSE;
                }
            } else if (cmd == DISK_PAGE_REPLY) {
                if (pos < 1 + header_len) {
                    buffer[pos - 2] = v;
                } else if (pos == 1 + header_len) {
                    buffer[pos - 2] = v;
                    if (!real_to_virtual(&fs->dg, buffer[1], &vda)) {
                        report_error("fs: load_image_bfs: could not convert "
                                     "real %u to virtual in file `%s`",
                                     buffer[1], filename);
                        fclose(fp);
                        return FALSE;
                    }
                    if (!(vda >= base_vda) || !(vda < end_vda)) {
                        report_error("fs: load_image_bfs: invalid vda %u "
                                     "for disk %u in file `%s`",
                                     vda, disk_num, filename);
                        fclose(fp);
                        return FALSE;
                    }

                    pg = &fs->pages[vda];
                    pg->page_vda = vda;

                    pg->header[0] = buffer[0];
                    pg->header[1] = buffer[1];
                } else if (pos < 2 + header_len + label_len) {
                    i = pos - 2 - header_len;
                    pg->label.r[i] = v;
                } else {
                    i = pos - 2 - header_len - label_len;
                    pg->data[2 * i] = (v >> 8);
                    pg->data[2 * i + 1] = (v);
                }
            } else {
                report_error("fs: load_image_bfs: invalid command %u "
                             "in file `%s`", cmd, filename);
                fclose(fp);
                return FALSE;
            }
        }
        pos++;
    }

    fclose(fp);
    return TRUE;

incomplete_file:
    report_error("fs: load_image_bfs: incomplete file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int fs_load_image(struct fs *fs, const char *filename,
                  uint16_t disk_num, int use_bfs_format)
{
    if (use_bfs_format) {
        return fs_load_image_bfs(fs, filename, disk_num);
    } else {
        return fs_load_image_aar(fs, filename, disk_num);
    }
}

/* Saves an AAR disk image.
 * The file to be written is given in the parameter `filename`.
 * This will write the disk number `disk_num`.
 * Returns TRUE on success.
 */
static
int fs_save_image_aar(const struct fs *fs,
                      const char *filename, uint16_t disk_num)
{
    FILE *fp;
    const struct page *pg;
    uint16_t i, j, vda, base_vda, w;
    uint16_t header_len, label_len;
    int c;

    fp = fopen(filename, "wb");
    if (!fp) {
        report_error("fs: save_image_aar: could not open file `%s` "
                     "for writing", filename);
        return FALSE;
    }

    base_vda = disk_num * fs->disk_length;
    header_len = sizeof(pg->header) / sizeof(uint16_t);
    label_len = sizeof(pg->label) / sizeof(uint16_t);

    for (i = 0; i < fs->disk_length; i++) {
        vda = base_vda + i;
        pg = &fs->pages[vda];

        /* Discard the first word. */
        c = fputc((int) (vda & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((vda >> 8) & 0xFF), fp);
        if (c == EOF) goto error;

        for (j = 0; j < header_len; j++) {
            w = pg->header[j];

            /* Process data in little-endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }

        for (j = 0; j < label_len; j++) {
            w = pg->label.r[j];

            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }

        for (j = 0; j < fs->sector_bytes; j++) {
            /* Byte swap the data here. */
            c = fputc((int) pg->data[j ^ 1], fp);
            if (c == EOF) goto error;
        }
    }

    fclose(fp);
    return TRUE;

error:
    report_error("fs: save_image_aar: error while writing `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

/* Saves an BFS disk image.
 * The file to be written is given in the parameter `filename`.
 * This will write the disk number `disk_num`.
 * Returns TRUE on success.
 */
static
int fs_save_image_bfs(const struct fs *fs,
                      const char *filename, uint16_t disk_num)
{
    FILE *fp;
    const struct page *pg;
    uint16_t i, j, vda, base_vda, w;
    uint16_t header_len, label_len;
    uint16_t buffer[32];
    int c;

    fp = fopen(filename, "wb");
    if (!fp) {
        report_error("fs: save_image_bfs: could not open file `%s` "
                     "for writing", filename);
        return FALSE;
    }

    base_vda = disk_num * fs->disk_length;
    header_len = sizeof(pg->header) / sizeof(uint16_t);
    label_len = sizeof(pg->label) / sizeof(uint16_t);

    buffer[0] = 7; /* length of the command. */
    buffer[1] = DISK_PARAMS_REPLY;
    buffer[2] = DIABLO_DISK_TYPE;
    buffer[3] = fs->dg.num_cylinders;
    buffer[4] = fs->dg.num_heads;
    buffer[5] = fs->dg.num_sectors;
    buffer[6] = 1; /* number of disks. */

    for (j = 0; j < buffer[0]; j++) {
        w = buffer[j];

        /* Discard the first word. */
        c = fputc((int) (w & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((w >> 8) & 0xFF), fp);
        if (c == EOF) goto error;
    }

    for (i = 0; i < fs->disk_length; i++) {
        vda = base_vda + i;
        pg = &fs->pages[vda];

        if (pg->label.s.version == VERSION_FREE
            || pg->label.s.version == VERSION_BAD)
            continue;

        buffer[0] = 2 + header_len + label_len + fs->dg.sector_words;
        buffer[1] = DISK_PAGE_REPLY;
        memcpy(&buffer[2], pg->header, sizeof(pg->header));
        memcpy(&buffer[2 + header_len], pg->label.r, sizeof(pg->label.r));

        for (j = 0; j < buffer[0] - fs->dg.sector_words; j++) {
            w = buffer[j];

            /* Discard the first word. */
            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }

        for (j = 0; j < fs->sector_bytes; j++) {
            /* Byte swap the data here. */
            c = fputc((int) pg->data[j ^ 1], fp);
            if (c == EOF) goto error;
        }
    }

    buffer[0] = 2;
    buffer[1] = END_OF_TRANSFER;
    for (j = 0; j < buffer[0]; j++) {
        w = buffer[j];

        /* Discard the first word. */
        c = fputc((int) (w & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((w >> 8) & 0xFF), fp);
        if (c == EOF) goto error;
    }

    fclose(fp);
    return TRUE;

error:
    report_error("fs: save_image_bfs: error while writing `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int fs_save_image(const struct fs *fs, const char *filename,
                  uint16_t disk_num, int use_bfs_format)
{
    if (use_bfs_format) {
        return fs_save_image_bfs(fs, filename, disk_num);
    } else {
        return fs_save_image_aar(fs, filename, disk_num);
    }
}

int fs_extract_file(const struct fs *fs, const char *name,
                    const char *output_filename)
{
    uint8_t buffer[MAX_PAGE_SIZE];
    struct open_file of;
    FILE *fp;
    size_t nbytes;
    size_t ret;

    if (!fs_open_ro(fs, name, &of)) {
        report_error("fs: extract_file: "
                     "could not open file: %s",
                     fs_error(of.error));
        return FALSE;
    }

    fp = fopen(output_filename, "wb");
    if (!fp) {
        report_error("fs: extract_file: could not open `%s` "
                     "for writing", output_filename);
        fs_close_ro(fs, &of);
        return FALSE;
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
        if (of.error < 0) {
            report_error("fs: extract_file: "
                         "error while reading: %s",
                         fs_error(of.error));
            goto error_read;
        }

        if (nbytes > 0) {
            ret = fwrite(buffer, 1, nbytes, fp);
            if (ret != nbytes) {
                report_error("fs: extract_file: error while writing "
                             "`%s`", output_filename);
                goto error_read;
            }
        }

        if (nbytes < sizeof(buffer)) break;
    }

    fclose(fp);
    fs_close_ro(fs, &of);
    return TRUE;

error_read:
    fclose(fp);
    fs_close_ro(fs, &of);
    return FALSE;
}

int fs_insert_file(struct fs *fs, const char *input_filename,
                   const char *name)
{
    uint8_t buffer[MAX_PAGE_SIZE];
    struct open_file of;
    FILE *fp;
    size_t nbytes;
    size_t ret;

    if (!fs_open(fs, name, "w", &of)) {
        report_error("fs: insert_file: "
                     "could not open file: %s",
                     fs_error(of.error));
        return FALSE;
    }

    fp = fopen(input_filename, "rb");
    if (!fp) {
        report_error("fs: insert_file: could not open `%s` "
                     "for reading", input_filename);
        fs_close(fs, &of);
        return FALSE;
    }

    while (TRUE) {
        nbytes = fread(buffer, 1, sizeof(buffer), fp);
        if (nbytes == 0) break;

        ret = fs_write(fs, &of, buffer, nbytes, TRUE);
        if ((ret != nbytes) || (of.error < 0)) {
            report_error("fs: insert_file: "
                         "error while writing: %s",
                         fs_error(of.error));
            goto error_write;
        }
    }

    fclose(fp);
    fs_close(fs, &of);
    return TRUE;

error_write:
    fclose(fp);
    fs_close(fs, &of);
    return FALSE;
}

int fs_copy(struct fs *fs, const char *src, const char *dst)
{
    struct file_entry fe;
    int found, error;

    if (!fs_resolve_name(fs, src, &found, &fe, NULL, NULL)) {
        report_error("fs: copy: "
                     "could not resolve name `%s`", src);
        return FALSE;
    }

    if (!found) {
        report_error("fs: copy: "
                     "could not find source file `%s`", src);
        return FALSE;
    }

    if (!fs_link(fs, dst, &fe, &error)) {
        report_error("fs: copy: "
                     "could not create link to `%s`: %s",
                     dst, fs_error(error));
        return FALSE;
    }

    return TRUE;
}
