
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"


/* Functions. */

void fs_increment_serial_number(struct fs *fs)
{
   fs->last_sn.word2++;
    if (fs->last_sn.word2 == 0) {
        fs->last_sn.word1++;
        fs->last_sn.word1 &= SN_PART1_MASK;
    }
}

void fs_update_disk_metadata(struct fs *fs)
{
    const struct page *pg;
    uint16_t vda, idx, bit;
    uint16_t word1, word2;

    memset(fs->bitmap, -1, fs->bitmap_size * sizeof(uint16_t));
    fs->free_pages = 0;
    fs->last_sn.word1 = 0;
    fs->last_sn.word2 = 0;

    for (vda = 0; vda < fs->length; vda++) {
        idx = IDX(vda);
        bit = BIT(vda);

        pg = &fs->pages[vda];
        if (pg->label.version == VERSION_FREE) {
            fs->bitmap[idx] &= ~(1 << bit);
            fs->free_pages++;
            continue;
        }

        if (pg->label.version == 0
            || pg->label.version == VERSION_BAD)
            continue;

        if (pg->label.file_pgnum == 0) {
            word1 = pg->label.sn.word1 & SN_PART1_MASK;
            word2 = pg->label.sn.word2;
            if (word1 > fs->last_sn.word1
                || ((word1 == fs->last_sn.word1)
                    && (word2 > fs->last_sn.word2))) {

                fs->last_sn.word1 = word1;
                fs->last_sn.word2 = word2;
            }
        }
    }
    fs_increment_serial_number(fs);
}

int fs_find_free_page(struct fs *fs, uint16_t *free_vda)
{
    const struct page *pg;
    uint16_t idx, bit;
    uint16_t vda;

    while (TRUE) {
        if (fs->free_pages == 0)
            return FALSE;

        for (idx = 0; idx < fs->bitmap_size; idx++) {
            if (fs->bitmap[idx] != 0xFFFF) break;
        }

        if (idx == fs->bitmap_size) {
            /* Something went wrong here, retry. */
            fs_update_disk_metadata(fs);
            continue;
        }

        for (bit = 0; bit < 16; bit++) {
            if (!(fs->bitmap[idx] & (1 << bit)))
                break;
        }

        fs->bitmap[idx] |= (1 << bit);
        fs->free_pages--;

        vda = VDA(idx, bit);
        pg = &fs->pages[vda];
        if (pg->label.version != VERSION_FREE) {
            /* Something went wrong here, retry. */
            fs_update_disk_metadata(fs);
            continue;
        }

        *free_vda = vda;
        return TRUE;
    }

    return FALSE;
}

int fs_update_disk_descriptor(struct fs *fs)
{
    struct file_entry fe;
    struct open_file of;
    uint8_t buffer[32];
    size_t nbytes;
    uint16_t i;

    fs_update_disk_metadata(fs);

    if (!fs_find_file(fs, "DiskDescriptor", &fe, NULL)) {
        report_error("fs: update_disk_descriptor: "
                     "could not find DiskDescriptor");
        return FALSE;
    }

    if (!fs_open(fs, &fe, &of)) {
        report_error("fs: update_disk_descriptor: "
                     "could not open DiskDescriptor");
        return FALSE;
    }
    /* Skip the leader page. */
    nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
    if (nbytes != PAGE_DATA_SIZE || of.error) {
        report_error("fs: update_disk_descriptor: "
                     "could not skip leader page");
        return FALSE;
    }

    memset(buffer, 0, sizeof(buffer));
    write_geometry(buffer, DESCR_OFF_GEOMETRY, &fs->dg);

    write_serial_number(buffer, DESCR_OFF_LAST_SN, &fs->last_sn);
    write_word_be(buffer, DESCR_OFF_DISKBT_SIZE, fs->bitmap_size);
    /* TODO: Use the correct versions kept. */
    write_word_be(buffer, DESCR_OFF_VERSIONS_KEPT, 0);
    write_word_be(buffer, DESCR_OFF_FREE_PAGES, fs->free_pages);

    nbytes = fs_write(fs, &of, buffer, sizeof(buffer), TRUE);
    if (nbytes != sizeof(buffer) || of.error) {
        report_error("fs: update_disk_descriptor: "
                     "could not write first part");
        return FALSE;
    }

    for (i = 0; i < fs->bitmap_size; i++) {
        write_word_be(buffer, 0, fs->bitmap[i]);
        nbytes = fs_write(fs, &of, buffer, 2, TRUE);
        if (nbytes != 2 || of.error) {
            report_error("fs: update_disk_descriptor: "
                         "could not write second part");
            return FALSE;
        }
    }
    if (!fs_trim(fs, &of)) {
        report_error("fs: update_disk_descriptor: "
                     "could not trim file");
        return FALSE;
    }

    return TRUE;
}
