
#include <stdint.h>
#include <string.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"


/* Functions. */

/* Counts the number of leading zeros in the binary representation
 * of the word `w`.
 * Returns the number of zeros.
 */
static
unsigned int count_leading_zeros(uint16_t w)
{
    unsigned int count;

    /* Shortcut for the case w = 0. */
    if (w == 0) return 16;
    count = 0;

    /* Use binary search on the bits. */
    if ((w & 0xFF00) == 0) {
        count += 8;
    } else {
        w >>= 8;
    }

    if ((w & 0x00F0) == 0) {
        count += 4;
    } else {
        w >>= 4;
    }

    if ((w & 0x000C) == 0) {
        count += 2;
    } else {
        w >>= 2;
    }

    if ((w & 0x0002) == 0) {
        count += 1;
    } else {
        w >>= 1;
    }

    if (w == 0) count++;
    return count;
}

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

int allocate_page(struct fs *fs, uint16_t *free_vda,
                  const uint16_t *last_vda)
{
    const struct page *pg;
    uint16_t idx, bit;
    unsigned int count, best, lz;
    uint16_t vda, best_vda, w;

    if (fs->free_pages == 0)
        return FALSE;

    if (last_vda) {
        /* Try to allocate consecutively. */
        vda = (*last_vda) + 1;
        idx = IDX(vda);
        bit = BIT(vda);

        if (!(fs->bitmap[idx] & (1 << bit))) {
            fs->bitmap[idx] |= (1 << bit);
            fs->free_pages--;
            *free_vda = vda;
            return TRUE;
        }
    }

    /* Now search for the largest contiguous sub-block of free pages. */
    count = 0;
    vda = VDA(0, 15);
    best = count;
    best_vda = vda;
    for (idx = 0; idx < fs->bitmap_size; idx++) {
        w = fs->bitmap[idx];

        if (count == 0) {
            /* Shortcut. */
            if (w == 0xFFFF) continue;
            vda = VDA(idx, 15);
        }

        bit = 16;
        while (bit > 0) {
            if (w & 0x8000) {
                if (count > best) {
                    best = count;
                    best_vda = vda;
                }
                bit--;
                w <<= 1;
                w |= 1;

                count = 0;
                if (w == 0xFFFF) {
                    /* Shortcut. */
                    break;
                }
                /* here bit > 0 necessarily */
                vda = VDA(idx, bit - 1);
            } else {
                lz = count_leading_zeros(w);
                count += lz;
                w <<= lz;
                w |= (1 << lz) - 1;
                bit -= lz;
            }
        }
    }

    if (best == 0) {
        report_error("fs: allocate_page: inconsistent metadata");
        update_disk_metadata(fs);
        return allocate_page(fs, free_vda, last_vda);
    }

    vda = best_vda;
    idx = IDX(vda);
    bit = BIT(vda);
    fs->bitmap[idx] |= (1 << bit);
    fs->free_pages--;
    pg = &fs->pages[vda];
    if (pg->label.version != VERSION_FREE) {
        report_error("fs: allocate_page: inconsistent metadata");
        update_disk_metadata(fs);
        return allocate_page(fs, free_vda, last_vda);
    }
    *free_vda = vda;
    return TRUE;
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
