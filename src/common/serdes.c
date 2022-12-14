
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/serdes.h"
#include "common/utils.h"

/* Functions. */

void serdes_initvar(struct serdes *sd)
{
    sd->buffer = NULL;
}

void serdes_destroy(struct serdes *sd)
{
    if (sd->buffer) free((void *) sd->buffer);
    sd->buffer = NULL;
}

int serdes_create(struct serdes *sd, size_t size, int extend)
{
    serdes_initvar(sd);

    if (unlikely(size == 0)) {
        report_error("serdes: create: "
                     "invalid size");
        serdes_destroy(sd);
        return FALSE;
    }

    sd->buffer = malloc(size);
    if (unlikely(!sd->buffer)) {
        report_error("serdes: create: "
                     "memory exhausted");
        serdes_destroy(sd);
        return FALSE;
    }

    sd->size = size;
    sd->pos = 0;
    sd->extend = extend;

    return TRUE;
}

void serdes_rewind(struct serdes *sd)
{
    sd->pos = 0;
}

int serdes_verify(struct serdes *sd)
{
    return (sd->pos <= sd->size);
}

int serdes_extend(struct serdes *sd, size_t size)
{
    uint8_t *new_buffer;

    if (unlikely(size <= sd->size)) {
        report_error("serdes: extend: size is too small");
        return FALSE;
    }

    new_buffer = realloc(sd->buffer, size);
    if (unlikely(!new_buffer)) {
        report_error("serdes: extend: memory exhausted");
        /* To prevent multiple attempts to extend. */
        sd->extend = FALSE;
        return FALSE;
    }
    sd->buffer = new_buffer;
    sd->size = size;
    return TRUE;
}

int serdes_read(struct serdes *sd, const char *filename)
{
    FILE *fp;
    size_t nbytes;

    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("serdes: read: cannot open `%s`",
                     filename);
        return FALSE;
    }

    sd->pos = 0;
    if (fseek(fp, 0L, SEEK_END) != 0) {
        report_error("serdes: read: could not determine "
                     "file size for `%s`", filename);
        fclose(fp);
        return FALSE;
    }
    sd->pos = (size_t) ftell(fp);

    if (sd->extend && (sd->pos > sd->size)) {
        if (unlikely(!serdes_extend(sd, sd->pos))) {
            report_error("serdes: read: could not extend");
            fclose(fp);
            return FALSE;
        }
    }

    rewind(fp);
    nbytes = fread(sd->buffer, 1, sd->size, fp);
    fclose(fp);

    if ((nbytes < sd->size) && (nbytes < sd->pos)) {
        report_error("serdes: read: could not read `%s`",
                     filename);
        return FALSE;
    }

    return TRUE;
}

int serdes_write(const struct serdes *sd, const char *filename)
{
    FILE *fp;
    size_t nbytes;

    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("serdes: write: cannot open `%s`",
                     filename);
        return FALSE;
    }

    nbytes = fwrite(sd->buffer, 1, sd->pos, fp);
    fclose(fp);

    if (unlikely(nbytes != sd->pos)) {
        report_error("serdes: write: could not write `%s`",
                     filename);
        return FALSE;
    }

    return TRUE;
}

uint8_t serdes_get8(struct serdes *sd)
{
    uint8_t v;

    if (sd->pos + 1 <= sd->size) {
        v = sd->buffer[sd->pos];
    } else {
        v = 0;
    }
    sd->pos++;
    return v;
}

uint16_t serdes_get16(struct serdes *sd)
{
    uint16_t v;

    if (sd->pos + 2 <= sd->size) {
        v = sd->buffer[sd->pos + 1];
        v |= ((uint16_t) sd->buffer[sd->pos]) << 8;
    } else {
        v = 0;
    }
    sd->pos += 2;
    return v;
}

uint32_t serdes_get32(struct serdes *sd)
{
    uint32_t v;

    if (sd->pos + 4 <= sd->size) {
        v = sd->buffer[sd->pos + 3];
        v |= ((uint32_t) sd->buffer[sd->pos + 2]) << 8;
        v |= ((uint32_t) sd->buffer[sd->pos + 1]) << 16;
        v |= ((uint32_t) sd->buffer[sd->pos]) << 24;
    } else {
        v = 0;
    }
    sd->pos += 4;
    return v;
}

int serdes_get_bool(struct serdes *sd)
{
    return (int) serdes_get8(sd);
}

void serdes_get8_array(struct serdes *sd, uint8_t *arr, size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        arr[i] = serdes_get8(sd);
    }
}

void serdes_get16_array(struct serdes *sd, uint16_t *arr, size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        arr[i] = serdes_get16(sd);
    }
}

void serdes_get32_array(struct serdes *sd, uint32_t *arr, size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        arr[i] = serdes_get32(sd);
    }
}

size_t serdes_get_string(struct serdes *sd, char *str, size_t size)
{
    size_t i;
    uint8_t v;

    for (i = 0; i < size; i++) {
        str[i] = (char) serdes_get8(sd);
        if (!str[i]) return i;
    }

    while (TRUE) {
        v = serdes_get8(sd);
        if (!v) return i;
        i++;
    }
}

void serdes_put8(struct serdes *sd, uint8_t v)
{
    if ((sd->pos + 1 > sd->size) && sd->extend) {
        serdes_extend(sd, 2 * sd->size);
    }

    if (sd->pos + 1 <= sd->size) {
        sd->buffer[sd->pos] = v;
    }
    sd->pos++;
}

void serdes_put16(struct serdes *sd, uint16_t v)
{
    if ((sd->pos + 2 > sd->size) && sd->extend) {
        serdes_extend(sd, 2 * sd->size);
    }

    if (sd->pos + 2 <= sd->size) {
        sd->buffer[sd->pos + 1] = v;
        sd->buffer[sd->pos] = (v >> 8);
    }
    sd->pos += 2;
}

void serdes_put32(struct serdes *sd, uint32_t v)
{
    if ((sd->pos + 4 > sd->size) && sd->extend) {
        serdes_extend(sd, 2 * sd->size);
    }

    if (sd->pos + 4 <= sd->size) {
        sd->buffer[sd->pos + 3] = v;
        sd->buffer[sd->pos + 2] = (v >> 8);
        sd->buffer[sd->pos + 1] = (v >> 16);
        sd->buffer[sd->pos] = (v >> 24);
    }
    sd->pos += 4;
}

void serdes_put_bool(struct serdes *sd, int val)
{
    serdes_put8(sd, (val) ? 1 : 0);
}

void serdes_put8_array(struct serdes *sd, const uint8_t *arr, size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        serdes_put8(sd, arr[i]);
    }
}

void serdes_put16_array(struct serdes *sd, const uint16_t *arr, size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        serdes_put16(sd, arr[i]);
    }
}

void serdes_put32_array(struct serdes *sd, const uint32_t *arr, size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        serdes_put32(sd, arr[i]);
    }
}

void serdes_put_string(struct serdes *sd, const char *str)
{
    size_t i;
    uint8_t v;

    i = 0;
    while (TRUE) {
        v = (uint8_t) str[i];
        serdes_put8(sd, v);
        if (!v) break;
        i++;
    }
}
