
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "simulator/disk.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define MAX_SECTORS      (406 * 2 * 12)

/* Functions. */

void disk_initvar(struct disk *dsk)
{
    unsigned int dnum;

    for (dnum = 0; dnum < NUM_DISK_DRIVES; dnum++) {
        dsk->drives[dnum].sectors = NULL;
    }
}

void disk_destroy(struct disk *dsk)
{
    struct disk_drive *dd;
    unsigned int dnum;

    for (dnum = 0; dnum < NUM_DISK_DRIVES; dnum++) {
        dd = &dsk->drives[dnum];
        if (dd->sectors)
            free((void *) dd->sectors);
        dd->sectors = NULL;
    }
}

int disk_create(struct disk *dsk)
{
    struct disk_drive *dd;
    unsigned int dnum;

    disk_initvar(dsk);

    for (dnum = 0; dnum < NUM_DISK_DRIVES; dnum++) {
        dd = &dsk->drives[dnum];

        dd->size = MAX_SECTORS;
        dd->sectors = (struct disk_sector *)
            malloc(MAX_SECTORS * sizeof(struct disk_sector));

        if (unlikely(!dd->sectors)) {
            report_error("disk: create: memory exhausted");
            disk_destroy(dsk);
            return FALSE;
        }

        dd->dg.num_cylinders = 203;
        dd->dg.num_heads = 2;
        dd->dg.num_sectors = 12;
        dd->length = dd->dg.num_cylinders;
        dd->length *= dd->dg.num_heads;
        dd->length *= dd->dg.num_sectors;
    }

    disk_reset(dsk);
    return TRUE;
}

int disk_load_image(struct disk *dsk, unsigned int drive_num,
                    const char *filename)
{
    FILE *fp;
    struct disk_drive *dd;
    uint16_t *ds;
    uint16_t i, j, max_j;
    uint16_t w;
    int c;

    if (drive_num >= NUM_DISK_DRIVES) {
        report_error("disk: load_image: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];

    fp = fopen(filename, "rb");
    if (!fp) {
        report_error("disk: load_image: could not open `%s`",
                     filename);
        return FALSE;
    }

    max_j = sizeof(struct disk_sector) / sizeof(uint16_t);
    for (i = 0; i < dd->length; i++) {
        ds = &dd->sectors[i].header[0];

        c = fgetc(fp);
        if (c == EOF) goto error;

        c = fgetc(fp);
        if (c == EOF) goto error;

        /* Discard the first word and use the loop index instead. */

        for (j = 0; j < max_j; j++) {
            /* Process data in little endian format. */
            c = fgetc(fp);
            if (c == EOF) goto error;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (c == EOF) goto error;
            w |= (uint16_t) ((c & 0xFF) << 8);

            ds[j] = w;
        }
    }

    c = fgetc(fp);
    if (c != EOF) goto error;

    fclose(fp);
    return TRUE;

error:
    report_error("disk: load_image: premature end of file in `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int disk_save_image(const struct disk *dsk, unsigned int drive_num,
                    const char *filename)
{
    FILE *fp;
    const struct disk_drive *dd;
    const uint16_t *ds;
    uint16_t i, j, max_j;
    uint16_t w;
    int c;

    if (drive_num >= NUM_DISK_DRIVES) {
        report_error("disk: save_image: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];

    fp = fopen(filename, "wb");
    if (!fp) {
        report_error("disk: save_image: could not open `%s`"
                     "for writing", filename);
        return FALSE;
    }

    max_j = sizeof(struct disk_sector) / sizeof(uint16_t);
    for (i = 0; i < dd->length; i++) {
        ds = (const uint16_t *) &dd->sectors[i].header[0];

        /* Discard the first word. */
        c = fputc((int) (i & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((i >> 8) & 0xFF), fp);
        if (c == EOF) goto error;

        for (j = 0; j < max_j; j++) {
            w = ds[j];

            /* Process data in little endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }
    }

    fclose(fp);
    return TRUE;

error:
    report_error("disk: save_image: error while writing `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

void disk_reset(struct disk *dsk)
{
    dsk->intr_cycle = -1;
    dsk->pending = 0;
}

uint16_t disk_read_kstat(struct disk *dsk)
{
    /* TODO: Implement this. */
    return dsk->kstat;
}

void disk_load_kstat(struct disk *dsk, uint16_t bus)
{
    /* TODO: Implement this. */
    dsk->kstat &= 0xFFF4U;
    dsk->kstat |= (bus & 0x0B);
    dsk->kstat |= ((~bus) & 0x04);
}

uint16_t disk_read_kdata(struct disk *dsk)
{
    /* TODO: Implement this. */
    return dsk->kdata;
}

void disk_load_kdata(struct disk *dsk, uint16_t bus)
{
    /* TODO: Implement this. */
    dsk->kdata = bus;
}

void disk_load_kcomm(struct disk *dsk, uint16_t bus)
{
    /* TODO: Implement this. */
    dsk->kcomm = (bus >> 10) & 0x1F;
}

void disk_load_kadr(struct disk *dsk, uint16_t bus)
{
    /* TODO: Implement this. */
    dsk->kadr = (bus & 0xFF);
}

void disk_strobe(struct disk *dsk)
{
    /* TODO: Implement this. */
}

void disk_increcno(struct disk *dsk)
{
    /* TODO: Implement this. */
}

void disk_clrstat(struct disk *dsk)
{
    /* TODO: Implement this. */
}

uint16_t disk_init(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

uint16_t disk_rwc(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

uint16_t disk_recno(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

uint16_t disk_xfrdat(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

uint16_t disk_swrnrdy(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

uint16_t disk_nfer(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

uint16_t disk_strobon(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    return 0;
}

void disk_block_task(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    dsk->pending &= ~(1 << task);
}

void disk_interrupt(struct disk *dsk)
{
    /* TODO: Implement this. */
}
