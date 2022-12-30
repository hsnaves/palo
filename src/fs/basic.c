
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Constants. */
#define TIME_MAGIC 2117503696 /* magic value to convert to Unix epoch. */

/* Functions. */

void fs_increment_serial_number(struct fs *fs)
{
   fs->last_sn.word2++;
    if (fs->last_sn.word2 == 0) {
        fs->last_sn.word1++;
        fs->last_sn.word1 &= SN_PART1_MASK;
    }
}

void fs_update_metadata(struct fs *fs)
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
            fs_update_metadata(fs);
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
            fs_update_metadata(fs);
            continue;
        }

        *free_vda = vda;
        return TRUE;
    }

    return FALSE;
}

int real_to_virtual(const struct geometry *dg, uint16_t rda, uint16_t *vda)
{
    uint16_t i, cylinder, head, sector, disk_num;

    cylinder = (rda >> 3) & 0x1FF;
    head = (rda >> 2) & 1;
    sector = (rda >> 12) & 0xF;
    disk_num = (rda >> 1) & 1;

    if ((disk_num >= dg->num_disks) || (cylinder >= dg->num_cylinders)
        || (head >= dg->num_heads) || (sector >= dg->num_sectors)
        || ((rda & 1) != 0))
        return FALSE;

    i = disk_num;
    i = i * dg->num_cylinders + cylinder;
    i = i * dg->num_heads + head;
    i = i * dg->num_sectors + sector;
    *vda = i;
    return TRUE;
}

int virtual_to_real(const struct geometry *dg, uint16_t vda, uint16_t *rda)
{
    uint16_t i, cylinder, head, sector, disk_num;

    i = vda;
    sector = i % dg->num_sectors;
    i /= dg->num_sectors;
    head = i % dg->num_heads;
    i /= dg->num_heads;
    cylinder = i % dg->num_cylinders;
    i /= dg->num_cylinders;
    disk_num = i % dg->num_disks;

    if (i >= dg->num_disks)
        return FALSE;

    *rda = (cylinder << 3) | (head << 2)
        | (sector << 12) | (disk_num << 1);
    return TRUE;
}

void read_name(const uint8_t *data, char name[NAME_LENGTH])
{
    size_t slen;

    slen = data[0];
    if (slen >= NAME_LENGTH)
        slen = NAME_LENGTH - 1;

    if (slen == 0) {
        name[0] = '\0';
        return;
    }

    memcpy(name, &data[1], slen - 1);
    name[slen - 1] = '\0';
}

void write_name(uint8_t *data, char name[NAME_LENGTH])
{
    size_t slen;

    slen = strlen(name);
    if (slen >= NAME_LENGTH)
        slen = NAME_LENGTH - 1;

    if (slen == 0) {
        data[0] = 0;
        data[1] = 0;
        return;
    }

    data[0] = (uint8_t) slen;
    memcpy(&data[1], name, slen - 1);
}

uint16_t read_word_be(const uint8_t *data, size_t offset)
{
    uint16_t w;
    w = (uint16_t) (data[offset + 1]);
    w |= (uint16_t) (data[offset] << 8);
    return w;
}

void write_word_be(uint8_t *data, size_t offset, uint16_t w)
{
    data[offset + 1] = (uint8_t) w;
    data[offset] = (uint8_t) (w >> 8);
}

void read_geometry(const uint8_t *data, size_t offset,
                   struct geometry *dg)
{
    dg->num_disks = read_word_be(data, offset);
    dg->num_cylinders = read_word_be(data, offset + 2);
    dg->num_heads = read_word_be(data, offset + 4);
    dg->num_sectors = read_word_be(data, offset + 6);
}

void write_geometry(uint8_t *data, size_t offset,
                    const struct geometry *dg)
{
    write_word_be(data, offset, dg->num_disks);
    write_word_be(data, offset + 2, dg->num_cylinders);
    write_word_be(data, offset + 4, dg->num_heads);
    write_word_be(data, offset + 6, dg->num_sectors);
}

time_t read_alto_time(const uint8_t *data, size_t offset)
{
    time_t time;

    time = (int) read_word_be(data, offset + 2);
    time += ((int) read_word_be(data, offset)) << 16;

    time += TIME_MAGIC;
    return time;
}

void write_alto_time(uint8_t *data, size_t offset, time_t time)
{
    time -= TIME_MAGIC;
    write_word_be(data, offset + 2, (uint16_t) time);
    write_word_be(data, offset, (uint16_t) (time >> 16));
}
