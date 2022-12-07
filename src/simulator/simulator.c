#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simulator/simulator.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define NUM_R_REGISTERS 32
#define NUM_S_REGISTERS (8 * 32)

/* Extra constants for the shifter. */
#define SHIFT_OP_NONE             0
#define SHIFT_OP_LEFT             1
#define SHIFT_OP_RIGHT            2
#define SHIFT_OP_ROTATE           3
#define SHIFT_MOD_NONE            0
#define SHIFT_MOD_MAGIC           1
#define SHIFT_MOD_DNS             2

/* Functions. */

void simulator_initvar(struct simulator *sim)
{
    sim->r = NULL;
    sim->s = NULL;
    sim->consts = NULL;
    sim->microcode = NULL;
}

void simulator_destroy(struct simulator *sim)
{
    if (sim->r) free((void *) sim->r);
    sim->r = NULL;

    if (sim->s) free((void *) sim->s);
    sim->s = NULL;

    if (sim->consts) free((void *) sim->consts);
    sim->microcode = NULL;

    if (sim->microcode) free((void *) sim->microcode);
    sim->microcode = NULL;
}

int simulator_create(struct simulator *sim)
{
    simulator_initvar(sim);

    sim->r = malloc(NUM_R_REGISTERS * sizeof(uint16_t));
    sim->s = malloc(NUM_S_REGISTERS * sizeof(uint16_t));
    sim->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    sim->microcode = (uint32_t *)
        malloc(4 * MICROCODE_SIZE * sizeof(uint32_t));

    if (unlikely(!sim->r || !sim->s || !sim->consts || !sim->microcode)) {
        report_error("sim: create: could not allocate memory");
        simulator_destroy(sim);
        return FALSE;
    }

    return TRUE;
}

int simulator_load_constant_rom(struct simulator *sim,
                                const char *filename)
{
    FILE *fp;
    uint16_t i;
    uint16_t val;
    int c;

    if (!filename) return TRUE;
    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("simulator: load_constant_rom: "
                     "cannot open `%s`", filename);
        return FALSE;
    }

    for (i = 0; i < CONSTANT_SIZE; i++) {
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val = (uint16_t) (c & 0xFF);

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint16_t) (c & 0xFF)) << 8;

        sim->consts[i] = val;
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("simulator: load_constant_rom: "
                     "invalid file size `%s`",
                     filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("simulator: load_constant_rom: "
                 "premature end of file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int simulator_load_microcode_rom(struct simulator *sim,
                                 const char *filename,
                                 unsigned int bank)
{
    FILE *fp;
    uint16_t i, offset;
    uint32_t val;
    int c;

    if (unlikely(bank >= 2)) {
        report_error("simulator: load_microcode_rom: "
                     "invalid bank `%u`", bank);
        return FALSE;
    }

    offset = (bank) ? MICROCODE_SIZE : 0;

    if (!filename) return TRUE;
    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("simulator: load_microcode_rom: "
                     "cannot open `%s`", filename);
        return FALSE;
    }

    for (i = 0; i < MICROCODE_SIZE; i++) {
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val = (uint32_t) (c & 0xFF);

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 8;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 16;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 24;

        sim->microcode[offset + i] = val;
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("simulator: load_microcode_rom: "
                     "invalid file size `%s`",
                     filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("simulator: load_microcode_rom: "
                 "premature end of file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

void simulator_reset(struct simulator *sim)
{
    memset(sim->r, 0, NUM_R_REGISTERS * sizeof(uint16_t));
    memset(sim->s, 0, NUM_S_REGISTERS * sizeof(uint16_t));

    sim->t = 0;
    sim->l = 0;
    sim->m = 0;
    sim->ir = 0;
    sim->aluC0 = FALSE;
    sim->rmr = 0xFFFF;
}

uint16_t compute_alu(int aluf, uint16_t bus, uint16_t t,
                     int skip, int *carry)
{
    uint32_t res;
    uint32_t a, b;

    a = (uint32_t) bus;
    b = (uint32_t) t;

    switch(aluf) {
    case ALU_BUS:
        res = a;
        break;
    case ALU_T:
        res = b;
        break;
    case ALU_BUS_OR_T:
        res = a | b;
        break;
    case ALU_BUS_AND_T:
    case ALU_BUS_AND_T_WB:
        res = a & b;
        break;
    case ALU_BUS_XOR_T:
        res = a ^ b;
        break;
    case ALU_BUS_PLUS_1:
        res = a + 1;
        break;
    case ALU_BUS_MINUS_1:
        res = a + 0xFFFF;
        break;
    case ALU_BUS_PLUS_T:
        res = a + b;
        break;
    case ALU_BUS_MINUS_T:
        res = a + ((~b) & 0xFFFF) + 1;
        break;
    case ALU_BUS_MINUS_T_MINUS_1:
        res = a + ((~b) & 0xFFFF);
        break;
    case ALU_BUS_PLUS_T_PLUS_1:
        res = a + b + 1;
        break;
    case ALU_BUS_PLUS_SKIP:
        res = a + ((uint32_t) skip);
        break;
    case ALU_BUS_AND_NOT_T:
        res = a & (~b) & 0xFFFF;
        break;
    default:
        res = 0xFFFF;
        break;
    }

    *carry = ((res & 0x10000) != 0);
    return (uint16_t) res;
}

uint16_t do_shift(uint16_t input, uint16_t t, int op,
                  int mod, int *nova_carry)
{
    uint16_t res;

    switch(op) {
    case SHIFT_OP_NONE:
        res = input;
        break;
    case SHIFT_OP_LEFT:
        res = input << 1;
        if (mod == SHIFT_MOD_MAGIC) {
            res |= (t & 0x8000) ? 1 : 0;
        } else if (mod == SHIFT_MOD_DNS) {
            /* Nova style shift */
            res |= (*nova_carry) ? 1 : 0;
            *nova_carry = (input & 0x8000) ? 1 : 0;
        }
        break;
    case SHIFT_OP_RIGHT:
        res = input >> 1;
        if (mod == SHIFT_MOD_MAGIC) {
            res |= (t & 1) ? 0x8000 : 0;
        } else if (mod == SHIFT_MOD_DNS) {
            /* Nova style shift */
            res |= (nova_carry) ? 0x8000 : 0;
            *nova_carry = input & 1;
        }
        break;
    case SHIFT_OP_ROTATE:
        res = (input << 8);
        res |= (input >> 8);
        break;
    }

    return 0;
}