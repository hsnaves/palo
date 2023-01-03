
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "fs/fs.h"
#include "fs/fs_internal.h"
#include "common/utils.h"

/* Constants. */
#define TIME_MAGIC 2117503696 /* magic value to convert to Unix epoch. */

/* Functions. */

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

void read_name(const uint8_t *data, size_t offset,
               char name[NAME_LENGTH])
{
    size_t slen;

    slen = data[offset];
    if (slen >= NAME_LENGTH)
        slen = NAME_LENGTH - 1;

    if (slen == 0) {
        name[0] = '\0';
        return;
    }

    memcpy(name, &data[offset + 1], slen - 1);
    name[slen - 1] = '\0';
}

void write_name(uint8_t *data, size_t offset,
                const char name[NAME_LENGTH])
{
    size_t slen;

    slen = strlen(name);
    if (slen >= NAME_LENGTH)
        slen = NAME_LENGTH - 1;

    if (slen == 0) {
        data[offset] = 0;
        data[offset + 1] = 0;
        return;
    }

    data[offset] = (uint8_t) (slen + 1);
    memcpy(&data[offset + 1], name, slen);
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

void read_serial_number(const uint8_t *data, size_t offset,
                        struct serial_number *sn)
{
    sn->word1 = read_word_be(data, offset);
    sn->word2 = read_word_be(data, offset + 2);
}

void write_serial_number(uint8_t *data, size_t offset,
                         const struct serial_number *sn)
{
    write_word_be(data, offset, sn->word1);
    write_word_be(data, offset + 2, sn->word2);
}

void read_file_entry(const uint8_t *data, size_t offset,
                     struct file_entry *fe)
{
    read_serial_number(data, offset, &fe->sn);
    fe->version = read_word_be(data, offset + 4);
    fe->blank = 0; /* read_word_be(data, offset + 6); */
    fe->leader_vda = read_word_be(data, offset + 8);
}

void write_file_entry(uint8_t *data, size_t offset,
                      const struct file_entry *fe)
{
    write_serial_number(data, offset, &fe->sn);
    write_word_be(data, offset + 4, fe->version);
    write_word_be(data, offset + 6, 0);
    write_word_be(data, offset + 8, fe->leader_vda);
}

void read_file_position(const uint8_t *data, size_t offset,
                        struct file_position *pos)
{
    pos->vda = read_word_be(data, offset);
    pos->pgnum = read_word_be(data, offset + 2);
    pos->pos = read_word_be(data, offset + 4);
}

void write_file_position(uint8_t *data, size_t offset,
                         const struct file_position *pos)
{
    write_word_be(data, offset, pos->vda);
    write_word_be(data, offset + 2, pos->pgnum);
    write_word_be(data, offset + 4, pos->pos);
}

void read_directory_entry(const uint8_t *data, size_t offset,
                          struct directory_entry *de)
{
    uint16_t w;
    w = read_word_be(data, offset);
    de->type = (w >> DIR_ENTRY_TYPE_SHIFT);
    de->length = (w & DIR_ENTRY_LEN_MASK);
    read_file_entry(data, offset + DIR_OFF_FILE_ENTRY, &de->fe);
    de->name_length = data[offset + DIR_OFF_NAME];
    read_name(data, offset + DIR_OFF_NAME, de->name);
}

void write_directory_entry(uint8_t *data, size_t offset,
                           const struct directory_entry *de)
{
    uint16_t w;
    w = (de->type << DIR_ENTRY_TYPE_SHIFT);
    w += (de->length & DIR_ENTRY_LEN_MASK);
    write_word_be(data, offset, w);
    write_file_entry(data, offset + DIR_OFF_FILE_ENTRY, &de->fe);
    write_name(data, offset + DIR_OFF_NAME, de->name);
    data[offset + DIR_OFF_NAME] = de->name_length;
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

void update_directory_entry_length(struct directory_entry *de)
{
    uint16_t len;
    len = de->name_length;
    if (len > NAME_LENGTH) len = NAME_LENGTH;
    de->length = (DIR_OFF_NAME + len + 1) / 2;
}
