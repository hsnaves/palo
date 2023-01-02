
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"


/* Functions. */

void increment_serial_number(struct fs *fs)
{
   fs->last_sn.word2++;
    if (fs->last_sn.word2 == 0) {
        fs->last_sn.word1++;
        fs->last_sn.word1 &= SN_PART1_MASK;
    }
}

void update_disk_metadata(struct fs *fs)
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
    increment_serial_number(fs);
}

int allocate_page(struct fs *fs, uint16_t *free_vda)
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
            report_error("fs: allocate_page: inconsistent metadata");
            update_disk_metadata(fs);
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
            report_error("fs: allocate_page: inconsistent metadata");
            update_disk_metadata(fs);
            continue;
        }

        *free_vda = vda;
        return TRUE;
    }

    return FALSE;
}

void free_pages(struct fs *fs, uint16_t vda, int follow)
{
   struct page *pg;
   uint16_t idx, bit, rda;

   if (vda >= fs->length) {
       report_error("fs: free_page: invalid VDA: %u", vda);
       return;
   }

   while (TRUE) {
       idx = IDX(vda);
       bit = BIT(vda);
       pg = &fs->pages[vda];

       pg->label.version = VERSION_FREE;
       fs->bitmap[idx] &= ~(1 << bit);
       fs->free_pages++;

       if (!follow) break;

       rda = pg->label.next_rda;
       real_to_virtual(&fs->dg, rda, &vda);
       if (vda == 0) break;
   }
}

int fs_update_disk_descriptor(struct fs *fs)
{
    struct open_file of;
    uint8_t buffer[32];
    size_t nbytes;
    uint16_t i;

    update_disk_metadata(fs);

    if (!fs_open(fs, "DiskDescriptor", "w+", &of))
        return FALSE;

    memset(buffer, 0, sizeof(buffer));
    write_geometry(buffer, DESCR_OFF_GEOMETRY, &fs->dg);

    write_serial_number(buffer, DESCR_OFF_LAST_SN, &fs->last_sn);
    write_word_be(buffer, DESCR_OFF_DISKBT_SIZE, fs->bitmap_size);
    /* TODO: Use the correct VERSIONS_KEPT. */
    write_word_be(buffer, DESCR_OFF_VERSIONS_KEPT, 0);
    write_word_be(buffer, DESCR_OFF_FREE_PAGES, fs->free_pages);

    nbytes = fs_write(fs, &of, buffer, sizeof(buffer), TRUE);
    if (nbytes != sizeof(buffer) || (of.error < 0))
        goto update_error;

    for (i = 0; i < fs->bitmap_size; i++) {
        write_word_be(buffer, 0, fs->bitmap[i]);
        nbytes = fs_write(fs, &of, buffer, 2, TRUE);
        if (nbytes != 2 || (of.error < 0))
            goto update_error;
    }
    if (!fs_truncate(fs, &of))
        goto update_error;

    return TRUE;

update_error:
    fs_close(fs, &of);
    return FALSE;
}
