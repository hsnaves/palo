PMU_OBJS := common/allocator.o common/table.o common/utils.o \
 assembler/assembler.o parser/lexer.o parser/parser.o microcode/microcode.o \
 pmu.o

PDIS_OBJS := common/utils.o common/allocator.o disassembler/disassembler.o \
 microcode/microcode.o pdis.o

PSIM_OBJS := common/utils.o simulator/simulator.o simulator/disk.o \
 simulator/display.o simulator/ethernet.o simulator/keyboard.o \
 simulator/mouse.o simulator/utils.o gui/gui.o microcode/microcode.o psim.o

OBJS := common/allocator.o common/table.o common/utils.o \
 assembler/assembler.o parser/lexer.o parser/parser.o microcode/microcode.o \
 pmu.o disassembler/disassembler.o pdis.o simulator/simulator.o \
 simulator/disk.o simulator/display.o simulator/ethernet.o \
 simulator/keyboard.o simulator/mouse.o simulator/utils.o gui/gui.o psim.o

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
microcode/microcode.o: microcode/microcode.c microcode/microcode.h \
 common/utils.h
pmu.o: pmu.c assembler/assembler.h parser/parser.h parser/lexer.h \
 common/allocator.h common/table.h parser/parser.h common/utils.h
disassembler/disassembler.o: disassembler/disassembler.c \
 disassembler/disassembler.h microcode/microcode.h common/allocator.h \
 common/utils.h
pdis.o: pdis.c disassembler/disassembler.h microcode/microcode.h \
 common/allocator.h common/utils.h
simulator/simulator.o: simulator/simulator.c simulator/simulator.h \
 microcode/microcode.h simulator/disk.h simulator/display.h \
 simulator/ethernet.h  simulator/keyboard.h simulator/mouse.h \
 simulator/utils.h common/utils.h
simulator/disk.o: simulator/disk.c simulator/disk.h simulator/utils.h \
 microcode/microcode.h common/utils.h
simulator/display.o: simulator/display.c simulator/display.h \
 simulator/utils.h microcode/microcode.h common/utils.h
simulator/ethernet.o: simulator/ethernet.c simulator/ethernet.h \
 microcode/microcode.h common/utils.h
simulator/keyboard.o: simulator/keyboard.c simulator/keyboard.h \
 common/utils.h
simulator/mouse.o: simulator/mouse.c simulator/mouse.h common/utils.h
simulator/utils.o: simulator/utils.c simulator/utils.h common/utils.h
gui/gui.o: gui/gui.c gui/gui.h common/utils.h
psim.o: psim.c simulator/simulator.h microcode/microcode.h \
 simulator/disk.h simulator/display.h simulator/ethernet.h \
 simulator/keyboard.h simulator/mouse.h common/utils.h
