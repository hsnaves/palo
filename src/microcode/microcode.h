
#ifndef __MICROCODE_MICROCODE_H
#define __MICROCODE_MICROCODE_H

#include <stddef.h>
#include <stdint.h>

#include "common/string_buffer.h"

/* Constants. */
#define CONSTANT_SIZE                    256
#define MICROCODE_SIZE                  1024
#define MEMORY_SIZE                    65536
#define ACSROM_SIZE                      256

/* Task types. */
#define TASK_EMULATOR                      0
#define TASK_DISK_SECTOR                   4
#define TASK_ETHERNET                      7
#define TASK_MEMORY_REFRESH                8
#define TASK_DISPLAY_WORD                  9
#define TASK_CURSOR                       10
#define TASK_DISPLAY_HORIZONTAL           11
#define TASK_DISPLAY_VERTICAL             12
#define TASK_PARITY                       13
#define TASK_DISK_WORD                    14
#define TASK_NUM_TASKS                    16
#define TASK_VALID_MASK               0x7F91
#define TASK_RAM_MASK                 0x0001

/* ALU functions (values of ALUF field in microinstruction). */
#define ALU_BUS                            0
#define ALU_T                              1
#define ALU_BUS_OR_T                       2
#define ALU_BUS_AND_T                      3
#define ALU_BUS_XOR_T                      4
#define ALU_BUS_PLUS_1                     5
#define ALU_BUS_MINUS_1                    6
#define ALU_BUS_PLUS_T                     7
#define ALU_BUS_MINUS_T                    8
#define ALU_BUS_MINUS_T_MINUS_1            9
#define ALU_BUS_PLUS_T_PLUS_1             10
#define ALU_BUS_PLUS_SKIP                 11
#define ALU_BUS_AND_T_WB                  12
#define ALU_BUS_AND_NOT_T                 13
#define ALU_UNDEFINED1                    14
#define ALU_UNDEFINED2                    15
/* Test if loads T directly from the ALU result. */
#define LOAD_T_FROM_ALU_MASK          0x1C65
#define LOAD_T_FROM_ALU(aluf) (((1 << (aluf)) & LOAD_T_FROM_ALU_MASK) != 0)

/* Possible values of the BS (bus select) field in the microinstruction. */
#define BS_READ_R                          0
#define BS_LOAD_R                          1
#define BS_NONE                            2
#define BS_TASK_SPECIFIC1                  3
#define BS_TASK_SPECIFIC2                  4
#define BS_READ_MD                         5
#define BS_READ_MOUSE                      6
#define BS_READ_DISP                       7
/* Ram task bus sources. */
#define BS_RAM_READ_S_LOCATION             3
#define BS_RAM_LOAD_S_LOCATION             4
/* KSEC (disk sector), KWD (disk word) bus sources. */
#define BS_DSK_READ_KSTAT                  3
#define BS_DSK_READ_KDATA                  4
/* ETHERNET bus sources. */
#define BS_ETH_EIDFCT                      4
/* Test if use the constant ROM. */
#define BS_USE_CROM(bs)          ((bs) >= 4)

/* Possible values of the F1 field in the microinstruction. */
#define F1_NONE                            0
#define F1_LOAD_MAR                        1
#define F1_TASK                            2
#define F1_BLOCK                           3
#define F1_LLSH1                           4
#define F1_LRSH1                           5
#define F1_LLCY8                           6
#define F1_CONSTANT                        7
/* F1 functions of ram tasks. */
#define F1_RAM_SWMODE                      8
#define F1_RAM_WRTRAM                      9
#define F1_RAM_RDRAM                      10
#define F1_RAM_LOAD_SRB                   11
/* F1 functions specific to the emulator task. */
#define F1_EMU_LOAD_RMR                   11
#define F1_EMU_LOAD_ESRB                  13
#define F1_EMU_RSNF                       14
#define F1_EMU_STARTF                     15
/* KSEC (disk sector), KWD (disk word) F1 functions. */
#define F1_DSK_STROBE                      9
#define F1_DSK_LOAD_KSTAT                 10
#define F1_DSK_INCRECNO                   11
#define F1_DSK_CLRSTAT                    12
#define F1_DSK_LOAD_KCOMM                 13
#define F1_DSK_LOAD_KADR                  14
#define F1_DSK_LOAD_KDATA                 15
/* ETHERNET F1 functions. */
#define F1_ETH_EILFCT                     11
#define F1_ETH_EPFCT                      12
#define F1_ETH_EWFCT                      13

/* Possible values of the F2 field in the microinstruction. */
#define F2_NONE                            0
#define F2_BUSEQ0                          1
#define F2_SHLT0                           2
#define F2_SHEQ0                           3
#define F2_BUS                             4
#define F2_ALUCY                           5
#define F2_STORE_MD                        6
#define F2_CONSTANT                        7
/* F2 functions specific to the emulator task. */
#define F2_EMU_BUSODD                      8
#define F2_EMU_MAGIC                       9
#define F2_EMU_LOAD_DNS                   10
#define F2_EMU_ACDEST                     11
#define F2_EMU_LOAD_IR                    12
#define F2_EMU_IDISP                      13
#define F2_EMU_ACSOURCE                   14
/* KSEC (disk sector), KWD (disk word) F2 functions. */
#define F2_DSK_INIT                        8
#define F2_DSK_RWC                         9
#define F2_DSK_RECNO                      10
#define F2_DSK_XFRDAT                     11
#define F2_DSK_SWRNRDY                    12
#define F2_DSK_NFER                       13
#define F2_DSK_STROBON                    14
/* ETHERNET F2 functions. */
#define F2_ETH_EODFCT                      8
#define F2_ETH_EOSFCT                      9
#define F2_ETH_ERBFCT                     10
#define F2_ETH_EEFCT                      11
#define F2_ETH_EBFCT                      12
#define F2_ETH_ECBFCT                     13
#define F2_ETH_EISFCT                     14
/* DWT (display word task) F2 functions. */
#define F2_DW_LOAD_DDR                     8
/* CURT (cursor task) F2 functions. */
#define F2_CUR_LOAD_XPREG                  8
#define F2_CUR_LOAD_CSR                    9
/* DHT (display horizontal task) F2 functions. */
#define F2_DH_EVENFIELD                    8
#define F2_DH_SETMODE                      9
/* DVT (display vertial task) F2 functions. */
#define F2_DV_EVENFIELD                    8

/* Extra constants. */
#define R_ZERO                             0
#define R_MASK                            31
#define NEXT_MASK_DSK_INIT           0x10000
#define NEXT_MASK_BUS                0x20000
#define NEXT_MASK_CONSTANT           0x40000

#define NUM_R_REGISTERS                   32
#define NUM_S_REGISTERS                   32
#define NUM_S_BANKS                        8
#define NUM_MICROCODE_BANKS                4
#define NUM_MEMORY_BANKS                   4

/* For the MPC. */
#define MPC_BANK_SHIFT                    10
#define MPC_BANK_MASK                  0x003
#define MPC_ADDR_MASK                  0x3FF

/* Decoding the microcode. */
#define MC_NEXT_S                          0
#define MC_NEXT_M                      0x3FF
#define MC_L_S                            10
#define MC_L_M                             1
#define MC_T_S                            11
#define MC_T_M                             1
#define MC_F2_S                           12
#define MC_F2_M                         0x0F
#define MC_F1_S                           16
#define MC_F1_M                         0x0F
#define MC_BS_S                           20
#define MC_BS_M                         0x07
#define MC_ALUF_S                         23
#define MC_ALUF_M                       0x0F
#define MC_RSEL_S                         27
#define MC_RSEL_M                       0x1F

#define MICROCODE_RSEL(mcode) (((mcode) >> MC_RSEL_S) & MC_RSEL_M)
#define MICROCODE_ALUF(mcode) (((mcode) >> MC_ALUF_S) & MC_ALUF_M)
#define MICROCODE_BS(mcode) (((mcode) >> MC_BS_S) & MC_BS_M)
#define MICROCODE_F1(mcode) (((mcode) >> MC_F1_S) & MC_F1_M)
#define MICROCODE_F2(mcode) (((mcode) >> MC_F2_S) & MC_F2_M)
#define MICROCODE_T(mcode) (((mcode) >> MC_T_S) & MC_T_M)
#define MICROCODE_L(mcode) (((mcode) >> MC_L_S) & MC_L_M)
#define MICROCODE_NEXT(mcode) (((mcode) >> MC_NEXT_S) & MC_NEXT_M)

/* For constant address. */
#define CONST_ADDR_RSEL(addr) ((addr) >> 3)
#define CONST_ADDR_BS(addr) ((addr) & 0x7)
#define CONST_ADDR(rsel, bs) ((((rsel) & 0x1F) << 3) | ((bs) & 0x7))

/* Data structures and types. */

/* Possible alto systems. */
enum system_type {
    ALTO_I,                       /* Alto I with 1K rom and 1K ram. */
    ALTO_II_1KROM,                /* Alto II with 1K rom and 1K ram. */
    ALTO_II_2KROM,                /* Alto II with 2K rom and 1K ram. */
    ALTO_II_3KRAM,                /* Alto II with 1K rom and 3K ram. */
};

/* Structure representing the partially decoded microcode. */
struct microcode {
    enum system_type sys_type;    /* The alto system type. */
    uint16_t address;             /* The address of the microcode
                                   * (including bank number).
                                   */
    uint32_t mcode;               /* The microcode itself. */
    uint8_t task;                 /* The current task. */

    uint16_t rsel;                /* The register select. */
    uint16_t aluf;                /* The alu function. */
    uint16_t bs;                  /* The bus source. */
    uint16_t f1;                  /* The F1 function. */
    uint16_t f2;                  /* The F2 function. */
    int load_t;                   /* Load T. */
    int load_l;                   /* Load L. */
    uint16_t next;                /* The address of the next
                                   * microinstruction.
                                   */
    int load_t_from_alu;          /* To load T from the ALU results. */
    int use_constant;             /* If either F1 or F2 specify a
                                   * constant.
                                   */
    int bs_use_crom;              /* If the BS fields uses constants. */
    uint16_t const_addr;          /* The address of the constant. */
    int ram_task;                 /* If this is a RAM task. */

    /* Extra fields only populated by microcode_decode(). */
    struct {
        int has_bus_assignment;   /* Decoding detected bus assignment. */
        int has_alu_assignment;   /* Decoding detected alu assignment. */
     } extra;
};

/* The type of decode operation to perform. */
enum decode_type {
    DECODE_CONST,                 /* To decode a constant value. */
    DECODE_REG,                   /* To decode a register. */
    DECODE_LABEL,                 /* To decode a label. */
    DECODE_MEMORY,                /* To decode a memory location. */
    DECODE_TASK,                  /* To decode a task. */
    DECODE_BOOL,                  /* To decode a boolean value. */
    DECODE_VALUE,                 /* A general 16-bit value. */
    DECODE_VALUE32,               /* A general 32-bit value. */
    DECODE_SVALUE32,              /* A signed 32-bit value. */
};

/* The general decoder callback function type.
 * It is used to decode the constants, the R registers names,
 * and the GOTO destination labels, etc. The value `val` is of type
 * uint32_t because it fits all the possible values one would like to
 * decode. The extra argument passed by the callback is in `arg`.
 * It is usually the value in `dec->arg`, but it could be different.
 */
struct decoder;
struct value_decoder;
typedef void (*value_decoder_cb)(struct value_decoder *vdec,
                                 enum decode_type dec_type, uint32_t val);

/* The value decoder. */
struct value_decoder {
    struct decoder *dec;          /* The parent decoder. */
    value_decoder_cb cb;          /* The callback to decode values. */
    void *arg;                    /* Extra argument for the callback. */
    struct value_decoder *next;   /* The next value_decoder, for chaining
                                   * decoders together.
                                   */
};

/* The microcode decoder.
 * This structure can also be used to decode other things, like
 * register values (for the debugger), etc.
 */
struct decoder {
    int error;                    /* Indicates an error was detected. */
    struct string_buffer *output; /* The output buffer. */
    struct microcode *mc;         /* Current instruction being decoded,
                                   * it might be NULL.
                                   */
    struct value_decoder *vdec;   /* The sub-decoder for decoding values. */
};

/* Tables. */

/* Names of microcode tasks. */
extern const char *TASK_NAMES[TASK_NUM_TASKS];

/* Functions. */

/* Predecodes the microcode.
 * The system type is given by `sys_type`, the address of the microcode
 * in `address` (this might include the bank number), the microcode
 * itself in `mcode` and the current task in `task`.
 */
void microcode_predecode(struct microcode *mc,
                         enum system_type sys_type,
                         uint16_t address, uint32_t mcode,
                         uint8_t task);

/* Decodes a value using the decoder callback.
 * The type of decoding is given by `dec_type`, and the value
 * is given by `val`.
 */
void decode_value(struct value_decoder *vdec,
                  enum decode_type dec_type, uint32_t val);

/* Decodes a value and pads with the necessary spaces to make
 * it a minimum length of `len`. All the other parameters are the
 * same as in decode_value().
 */
void decode_value_padded(struct value_decoder *vdec,
                         enum decode_type dec_type, uint32_t val,
                         size_t len);

/* Decodes a tagged value. The `tag` parameters specifies the tag name.
 * All the other parameters are the same as in decode_value().
 */
void decode_tagged_value(struct value_decoder *vdec, const char *tag,
                         enum decode_type dec_type, uint32_t val);

/* Decodes the microinstruction in the decoder `dec`. */
void decode_microcode(struct decoder *dec);


#endif /* __MICROCODE_MICROCODE_H */
