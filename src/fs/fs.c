
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "common/utils.h"

/* Functions. */

void fs_initvar(struct fs *fs)
{
    fs->pages = NULL;
    fs->ref_count = NULL;
    fs->bitmap = NULL;
}

void fs_destroy(struct fs *fs)
{
    if (fs->pages) free((void *) fs->pages);
    fs->pages = NULL;

    if (fs->ref_count) free((void *) fs->ref_count);
    fs->ref_count = NULL;

    if (fs->bitmap) free((void *) fs->bitmap);
    fs->bitmap = NULL;
}

int fs_create(struct fs *fs, struct geometry dg)
{
    size_t size;
    fs_initvar(fs);

    if (unlikely(dg.num_disks > 2
                 || dg.num_heads > 2
                 || dg.num_sectors > 15
                 || dg.num_cylinders >= 512)) {
        report_error("fs: create: invalid disk geometry");
        return FALSE;
    }

    fs->dg = dg;

    fs->length = dg.num_disks * dg.num_cylinders
        * dg.num_heads * dg.num_sectors;
    fs->bitmap_size = (fs->length >> 4);
    if (fs->length & 0xF) fs->bitmap_size++;

    size = ((size_t) fs->length) * sizeof(struct page);
    fs->pages = (struct page *) malloc(size);

    size = ((size_t) fs->length) * sizeof(uint16_t);
    fs->ref_count = (uint16_t *) malloc(size);

    size = ((size_t) fs->bitmap_size) * sizeof(uint16_t);
    fs->bitmap = (uint16_t *) malloc(size);

    if (unlikely(!fs->pages || !fs->ref_count || !fs->bitmap)) {
        report_error("fs: create: memory exhausted");
        fs_destroy(fs);
        return FALSE;
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

int fs_load_image(struct fs *fs, const char *filename)
{
    FILE *fp;
    struct page *pg;
    uint16_t vda, j, meta_len;
    uint16_t w, *meta_ptr;
    int c;

    fp = fopen(filename, "rb");
    if (!fp) {
        report_error("fs: load_image: could not open `%s`",
                     filename);
        return FALSE;
    }

    fs->checked = FALSE;
    meta_len = offsetof(struct page, data) /  sizeof(uint16_t);
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        c = fgetc(fp);
        if (c == EOF) goto error_eof;

        c = fgetc(fp);
        if (c == EOF) goto error_eof;

        /* Discard the first word and use the loop index instead. */
        pg->page_vda = vda;

        meta_ptr = (uint16_t *) pg;
        for (j = 1; j < meta_len; j++) {
            /* Process data in little-endian format. */
            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (c == EOF) goto error_eof;
            w |= (uint16_t) ((c & 0xFF) << 8);

            meta_ptr[j] = w;
        }

        for (j = 0; j < PAGE_DATA_SIZE; j++) {
            c = fgetc(fp);
            if (c == EOF) goto error_eof;

            /* Byte swap the data here. */
            pg->data[j ^ 1] = (uint8_t) c;
        }
    }

    c = fgetc(fp);
    if (c != EOF) {
        report_error("fs: load_image: "
                     "file `%s` longer than expected", filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("fs: load_image: "
                 "premature end of file in `%s`", filename);
    fclose(fp);
    return FALSE;
}

int fs_save_image(const struct fs *fs, const char *filename)
{
    FILE *fp;
    const struct page *pg;
    uint16_t vda, j, meta_len, w;
    const uint16_t *meta_ptr;
    int c;

    fp = fopen(filename, "wb");
    if (!fp) {
        report_error("fs: save_image: could not open file `%s` "
                     "for writing", filename);
        return FALSE;
    }

    meta_len = offsetof(struct page, data) /  sizeof(uint16_t);
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        /* Discard the first word. */
        c = fputc((int) (vda & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((vda >> 8) & 0xFF), fp);
        if (c == EOF) goto error;

        meta_ptr = (const uint16_t *) pg;
        for (j = 1; j < meta_len; j++) {
            w = meta_ptr[j];

            /* Process data in little-endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }

        for (j = 0; j < PAGE_DATA_SIZE; j++) {
            /* Byte swap the data here. */
            c = fputc((int) pg->data[j ^ 1], fp);
            if (c == EOF) goto error;
        }
    }

    fclose(fp);
    return TRUE;

error:
    report_error("fs: save_image: error while writing `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int fs_extract_file(const struct fs *fs, const char *name,
                    const char *output_filename)
{
    uint8_t buffer[PAGE_DATA_SIZE];
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
    uint8_t buffer[PAGE_DATA_SIZE];
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
