
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
    fs->bitmap = NULL;
}

void fs_destroy(struct fs *fs)
{
    if (fs->pages) free((void *) fs->pages);
    fs->pages = NULL;

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

    size = ((size_t) fs->bitmap_size) * sizeof(uint16_t);
    fs->bitmap = (uint16_t *) malloc(size);

    if (unlikely(!fs->pages || !fs->bitmap)) {
        report_error("fs: create: memory exhausted");
        fs_destroy(fs);
        return FALSE;
    }

    fs->free_pages = 0xFFFF;
    fs->last_sn.word1 = 0;
    fs->last_sn.word2 = 0;

    return TRUE;
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

    meta_len = (sizeof(pg->header) + sizeof(pg->label)) / sizeof(uint16_t);
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
            /* Process data in little endian format. */
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

    meta_len = (sizeof(pg->header) + sizeof(pg->label)) / sizeof(uint16_t);
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

            /* Process data in little endian format. */
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

int fs_extract_file(const struct fs *fs, const struct file_entry *fe,
                    const char *output_filename, int include_leader_page)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    struct open_file of;
    FILE *fp;
    size_t nbytes;
    size_t ret;

    if (!fs_open(fs, fe, &of)) {
        report_error("fs: extract_file: "
                     "could not open file");
        return FALSE;
    }

    fp = fopen(output_filename, "wb");
    if (!fp) {
        report_error("fs: extract_file: could not open `%s` "
                     "for writing", output_filename);
        return FALSE;
    }

    if (!include_leader_page) {
        /* Skip the leader page. */
        nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
        if (nbytes != PAGE_DATA_SIZE || of.error) {
            report_error("fs: extract_file: "
                         "error while discarding leader page");
            fclose(fp);
            return FALSE;
        }
    }

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, sizeof(buffer));
        if (of.error) {
            report_error("fs: extract_file: error while reading");
            fclose(fp);
            return FALSE;
        }

        if (nbytes > 0) {
            ret = fwrite(buffer, 1, nbytes, fp);
            if (ret != nbytes) {
                report_error("fs: extract_file: error while writing "
                             "`%s`", output_filename);
                fclose(fp);
                return FALSE;
            }
        }

        if (nbytes < sizeof(buffer)) break;
    }

    fclose(fp);
    return TRUE;
}
