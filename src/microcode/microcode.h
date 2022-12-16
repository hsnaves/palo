
#ifndef __MICROCODE_MICROCODE_H
#define __MICROCODE_MICROCODE_H

#include <stddef.h>
#include <stdint.h>

/* Constants. */
#define CONSTANT_SIZE            256
#define MICROCODE_SIZE          1024
#define MEMORY_SIZE            65536

/* Task types. */
#define TASK_EMULATOR              0
#define TASK_DISK_SECTOR          04
#define TASK_ETHERNET             07
#define TASK_MEMORY_REFRESH      010
#define TASK_DISPLAY_WORD        011
#define TASK_CURSOR              012
#define TASK_DISPLAY_HORIZONTAL  013
#define TASK_DISPLAY_VERTICAL    014
#define TASK_PARITY              015
#define TASK_DISK_WORD           016
#define TASK_NUM_TASKS           020
#define TASK_VALID_MASK       0x7F91
#define TASK_RAM_MASK         0x0001

/* ALU functions (values of ALUF field in microinstruction). */
#define ALU_BUS                    0
#define ALU_T                     01
#define ALU_BUS_OR_T              02
#define ALU_BUS_AND_T             03
#define ALU_BUS_XOR_T             04
#define ALU_BUS_PLUS_1            05
#define ALU_BUS_MINUS_1           06
#define ALU_BUS_PLUS_T            07
#define ALU_BUS_MINUS_T          010
#define ALU_BUS_MINUS_T_MINUS_1  011
#define ALU_BUS_PLUS_T_PLUS_1    012
#define ALU_BUS_PLUS_SKIP        013
#define ALU_BUS_AND_T_WB         014
#define ALU_BUS_AND_NOT_T        015
#define ALU_UNDEFINED1           016
#define ALU_UNDEFINED2           017
/* Test if loads T directly from the ALU result. */
#define LOAD_T_FROM_ALU_MASK  0x1C65
#define LOAD_T_FROM_ALU(aluf) (((1 << (aluf)) & LOAD_T_FROM_ALU_MASK) != 0)

/* Possible values of the BS (bus select) field in the microinstruction. */
#define BS_READ_R                  0
#define BS_LOAD_R                 01
#define BS_NONE                   02
#define BS_TASK_SPECIFIC1         03
#define BS_TASK_SPECIFIC2         04
#define BS_READ_MD                05
#define BS_READ_MOUSE             06
#define BS_READ_DISP              07
/* Ram task bus sources. */
#define BS_RAM_READ_S_LOCATION    03
#define BS_RAM_LOAD_S_LOCATION    04
/* KSEC (disk sector), KWD (disk word) bus sources. */
#define BS_DSK_READ_KSTAT         03
#define BS_DSK_READ_KDATA         04
/* ETHERNET bus sources. */
#define BS_ETH_EIDFCT             04
/* Test if use the constant ROM. */
#define BS_USE_CROM(bs) ((bs) >= 4)

/* Possible values of the F1 field in the microinstruction. */
#define F1_NONE                    0
#define F1_LOAD_MAR               01
#define F1_TASK                   02
#define F1_BLOCK                  03
#define F1_LLSH1                  04
#define F1_LRSH1                  05
#define F1_LLCY8                  06
#define F1_CONSTANT               07
/* F1 functions specific to the emulator task. */
#define F1_EMU_SWMODE            010
#define F1_EMU_WRTRAM            011
#define F1_EMU_RDRAM             012
#define F1_EMU_LOAD_RMR          013
#define F1_EMU_LOAD_ESRB         015
#define F1_EMU_RSNF              016
#define F1_EMU_STARTF            017
/* KSEC (disk sector), KWD (disk word) F1 functions. */
#define F1_DSK_STROBE            011
#define F1_DSK_LOAD_KSTAT        012
#define F1_DSK_INCRECNO          013
#define F1_DSK_CLRSTAT           014
#define F1_DSK_LOAD_KCOMM        015
#define F1_DSK_LOAD_KADR         016
#define F1_DSK_LOAD_KDATA        017
/* ETHERNET F1 functions. */
#define F1_ETH_EILFCT            013
#define F1_ETH_EPFCT             014
#define F1_ETH_EWFCT             015

/* Possible values of the F2 field in the microinstruction. */
#define F2_NONE                    0
#define F2_BUSEQ0                 01
#define F2_SHLT0                  02
#define F2_SHEQ0                  03
#define F2_BUS                    04
#define F2_ALUCY                  05
#define F2_STORE_MD               06
#define F2_CONSTANT               07
/* F2 functions specific to the emulator task. */
#define F2_EMU_BUSODD            010
#define F2_EMU_MAGIC             011
#define F2_EMU_LOAD_DNS          012
#define F2_EMU_ACDEST            013
#define F2_EMU_LOAD_IR           014
#define F2_EMU_IDISP             015
#define F2_EMU_ACSOURCE          016
/* KSEC (disk sector), KWD (disk word) F2 functions. */
#define F2_DSK_INIT              010
#define F2_DSK_RWC               011
#define F2_DSK_RECNO             012
#define F2_DSK_XFRDAT            013
#define F2_DSK_SWRNRDY           014
#define F2_DSK_NFER              015
#define F2_DSK_STROBON           016
/* ETHERNET F2 functions. */
#define F2_ETH_EODFCT            010
#define F2_ETH_EOSFCT            011
#define F2_ETH_ERBFCT            012
#define F2_ETH_EEFCT             013
#define F2_ETH_EBFCT             014
#define F2_ETH_ECBFCT            015
#define F2_ETH_EISFCT            016
/* DWT (display word task) F2 functions. */
#define F2_DW_LOAD_DDR           010
/* CURT (cursor task) F2 functions. */
#define F2_CUR_LOAD_XPREG        010
#define F2_CUR_LOAD_CSR          011
/* DHT (display horizontal task) F2 functions. */
#define F2_DH_EVENFIELD          010
#define F2_DH_SETMODE            011
/* DVT (display vertial task) F2 functions. */
#define F2_DV_EVENFIELD          010

/* Extra constants. */
#define R_ZERO                     0
#define R_MASK                   037
#define NEXT_MASK_DSK_INIT   0x10000
#define NEXT_MASK_BUS        0x20000
#define NEXT_MASK_CONSTANT   0x40000

/* Decoding the microcode. */
#define MICROCODE_RSEL(microcode) (((microcode) >> 27) & R_MASK)
#define MICROCODE_ALUF(microcode) (((microcode) >> 23) & 0x0F)
#define MICROCODE_BS(microcode) (((microcode) >> 20) & 0x07)
#define MICROCODE_F1(microcode) (((microcode) >> 16) & 0x0F)
#define MICROCODE_F2(microcode) (((microcode) >> 12) & 0x0F)
#define MICROCODE_T(microcode) (((microcode) >> 11) & 0x01)
#define MICROCODE_L(microcode) (((microcode) >> 10) & 0x01)
#define MICROCODE_NEXT(microcode) ((microcode) & 0x3FF)

/* For constant address. */
#define CONST_ADDR_RSEL(addr) ((addr) >> 3)
#define CONST_ADDR_BS(addr) ((addr) & 0x7)
#define CONST_ADDR(rsel, bs) ((((rsel) & 0x1F) << 3) | ((bs) & 0x7))

/* Data structures and types. */

/* A buffer used by the decoder. */
struct decode_buffer {
    char *buf;                    /* The character buffer. */
    size_t buf_size;              /* The buffer size in bytes. */
    size_t pos;                   /* The current buffer position. */
    size_t len;                   /* Total length of the string. */
};

/* The general decoder callback function type.
 * It is used to decode the constants, the R registers names,
 * and the GOTO destination labels.
 */
struct decoder;
typedef void (*decoder_cb)(const struct decoder *dec, uint16_t val,
                           struct decode_buffer *output);

/* The microcode decoder. */
struct decoder {
    uint16_t address;             /* The address of the microcode. */
    uint32_t microcode;           /* The microcode itself. */
    uint8_t task;                 /* The current task. */
    void *arg;                    /* Extra parameter used by the
                                   * callbacks.
                                   */

    decoder_cb const_cb;          /* To decode constants. */
    decoder_cb reg_cb;            /* To decode R register names. */
    decoder_cb goto_cb;           /* To decode GOTO statements. */
};

/* Functions. */

/* Function to guess the task from the microcode.
 * Returns a mask of possible tasks that can run the microcode.
 */
uint16_t microcode_guess_tasks(uint32_t microcode);

/* Which bits of the following microcode's NEXT field can be
 * modified by the current microcode. The current task is
 * indicate by the `task` parameter.
 * Returns the bit mask of modified by the microcode.
 * The result is 32 bits so it can accomodate some other meta
 * bits such as NEXT_MASK_DSK_INIT, NEXT_MASK_BUS, and
 * NEXT_MASK_CONSTANT.
 */
uint32_t microcode_next_mask(uint32_t microcode, uint8_t  task);

/* Resets the decode buffer. */
void decode_buffer_reset(struct decode_buffer *buf);

/* Prints a string (as in printf() function) to the buffer. */
void decode_buffer_print(struct decode_buffer *buf,
                         const char *fmt, ...)
    __attribute__((format (printf, 2, 3)));

/* Decodes the microinstruction from the decoder `dec` into the
 * output buffer `output`.
 */
void decoder_decode(const struct decoder *dec,
                    struct decode_buffer *output);


#endif /* __MICROCODE_MICROCODE_H */
