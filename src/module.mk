PMU_OBJS := common/allocator.o common/table.o common/utils.o \
  assembler/assembler.o parser/lexer.o parser/parser.o pmu.o

PDIS_OBJS := common/utils.o disassembler/disassembler.o pdis.o

OBJS := $(PMU_OBJS) disassembler/disassembler.o pdis.o

assembler/assembler.o: assembler/assembler.c assembler/assembler.h \
 parser/parser.h parser/lexer.h common/allocator.h common/table.h \
 microcode/microcode.h common/utils.h
common/allocator.o: common/allocator.c common/allocator.h common/utils.h
common/table.o: common/table.c common/table.h common/utils.h
common/utils.o: common/utils.c common/utils.h
parser/lexer.o: parser/lexer.c parser/lexer.h common/allocator.h \
 common/table.h common/utils.h
parser/parser.o: parser/parser.c parser/parser.h parser/lexer.h \
 common/allocator.h common/table.h common/utils.h
pmu.o: pmu.c assembler/assembler.h parser/parser.h parser/lexer.h \
 common/allocator.h common/table.h parser/parser.h common/utils.h

disassembler/disassembler.o: disassembler/disassembler.c \
 disassembler/disassembler.h microcode/microcode.h common/utils.h
pdis.o: pdis.c disassembler/disassembler.h common/utils.h
