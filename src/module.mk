ASSEMBLER_OBJS := assembler/assembler.o assembler/objfile.o
COMMON_OBJS := common/allocator.o common/table.o common/serdes.o \
 common/string_buffer.o common/utils.o
DEBUGGER_OBJS := debugger/debugger.o debugger/cmd.o
FS_OBJS := fs/basic.o fs/check.o fs/dir.o fs/disk.o fs/file.o fs/fs.o \
 fs/meta.o fs/scan.o fs/print.o
GUI_OBJS := gui/gui.o
MICROCODE_OBJS := microcode/microcode.o microcode/nova.o
PARSER_OBJS := parser/parser.o parser/lexer.o
SIMULATOR_OBJS := simulator/simulator.o simulator/disk.o \
 simulator/display.o simulator/ethernet.o simulator/keyboard.o \
 simulator/mouse.o simulator/intr.o simulator/rom.o


PMU_OBJS := $(ASSEMBLER_OBJS) $(COMMON_OBJS) $(PARSER_OBJS) \
 microcode/microcode.o pmu.o
PAR_OBJS := $(FS_OBJS) common/utils.o par.o
PALOS_OBJS := $(DEBUGGER_OBJS) $(GUI_OBJS) $(MICROCODE_OBJS) \
 $(SIMULATOR_OBJS) common/serdes.o common/string_buffer.o common/utils.o \
 palos.o
OBJS := $(ASSEMBLER_OBJS) $(COMMON_OBJS) $(DEBUGGER_OBJS) $(FS_OBJS) \
 $(GUI_OBJS) $(MICROCODE_OBJS) $(PARSER_OBJS) $(SIMULATOR_OBJS) \
 pmu.o par.o palos.o


assembler/assembler.o: assembler/assembler.c assembler/assembler.h \
 parser/parser.h parser/lexer.h common/allocator.h common/table.h \
 assembler/objfile.h microcode/microcode.h common/string_buffer.h \
 common/serdes.h common/utils.h
assembler/objfile.o: assembler/objfile.c assembler/objfile.h \
 microcode/microcode.h common/string_buffer.h common/allocator.h \
 common/table.h common/serdes.h common/utils.h
common/allocator.o: common/allocator.c common/allocator.h common/utils.h
common/table.o: common/table.c common/table.h common/utils.h
common/serdes.o: common/serdes.c common/serdes.h common/utils.h
common/string_buffer.o: common/string_buffer.c common/string_buffer.h
common/utils.o: common/utils.c common/utils.h
parser/lexer.o: parser/lexer.c parser/lexer.h common/allocator.h \
 common/table.h common/utils.h
parser/parser.o: parser/parser.c parser/parser.h parser/lexer.h \
 common/allocator.h common/table.h common/utils.h
microcode/microcode.o: microcode/microcode.c microcode/microcode.h \
 common/string_buffer.h common/utils.h
microcode/nova.o: microcode/nova.c microcode/nova.h microcode/microcode.h \
 common/string_buffer.h common/utils.h
pmu.o: pmu.c assembler/assembler.h parser/parser.h parser/lexer.h \
 common/allocator.h common/table.h assembler/objfile.h \
 microcode/microcode.h common/string_buffer.h common/serdes.h \
 common/utils.h
fs/basic.o: fs/basic.c fs/fs.h fs/fs_internal.h common/utils.h
fs/check.o: fs/check.c fs/fs.h fs/fs_internal.h common/utils.h
fs/dir.o: fs/dir.c fs/fs.h fs/fs_internal.h common/utils.h
fs/disk.o: fs/disk.c fs/fs.h fs/fs_internal.h common/utils.h
fs/file.o: fs/file.c fs/fs.h fs/fs_internal.h common/utils.h
fs/meta.o: fs/meta.c fs/fs.h fs/fs_internal.h common/utils.h
fs/fs.o: fs/fs.c fs/fs.h common/utils.h
fs/print.o: fs/print.c fs/fs.h common/utils.h
fs/scan.o: fs/scan.c fs/fs.h fs/fs_internal.h common/utils.h
par.o: par.c fs/fs.h common/utils.h
simulator/simulator.o: simulator/simulator.c simulator/simulator.h \
 microcode/microcode.h common/string_buffer.h microcode/nova.h \
 simulator/disk.h common/serdes.h simulator/display.h simulator/ethernet.h \
 simulator/keyboard.h simulator/mouse.h common/utils.h simulator/intr.h \
 simulator/rom.h
simulator/disk.o: simulator/disk.c simulator/disk.h common/serdes.h \
 common/string_buffer.h simulator/intr.h microcode/microcode.h \
 common/utils.h
simulator/display.o: simulator/display.c simulator/display.h \
 common/serdes.h common/string_buffer.h simulator/intr.h \
 microcode/microcode.h common/utils.h
simulator/ethernet.o: simulator/ethernet.c simulator/ethernet.h \
 common/serdes.h common/string_buffer.h simulator/intr.h \
 microcode/microcode.h common/utils.h
simulator/keyboard.o: simulator/keyboard.c simulator/keyboard.h \
 common/serdes.h common/string_buffer.h common/utils.h
simulator/mouse.o: simulator/mouse.c simulator/mouse.h common/serdes.h \
 common/string_buffer.h common/utils.h
simulator/intr.o: simulator/intr.c simulator/intr.h common/utils.h
simulator/rom.o: simulator/rom.c simulator/rom.h microcode/microcode.h \
 common/string_buffer.h
gui/gui.o: gui/gui.c gui/gui.h simulator/simulator.h microcode/microcode.h \
 common/string_buffer.h microcode/nova.h simulator/disk.h common/serdes.h \
 simulator/display.h simulator/ethernet.h  simulator/keyboard.h \
 simulator/mouse.h common/utils.h
debugger/debugger.o: debugger/debugger.c debugger/debugger.h \
 simulator/simulator.h microcode/microcode.h common/string_buffer.h \
 microcode/nova.h simulator/disk.h common/serdes.h simulator/display.h \
 simulator/ethernet.h simulator/keyboard.h simulator/mouse.h \
 common/utils.h gui/gui.h
debugger/cmd.o: debugger/cmd.c debugger/debugger.h simulator/simulator.h \
 microcode/microcode.h common/string_buffer.h microcode/nova.h \
 simulator/disk.h common/serdes.h simulator/display.h simulator/ethernet.h \
 simulator/keyboard.h simulator/mouse.h common/utils.h gui/gui.h \
 simulator/intr.h
palos.o: palos.c simulator/simulator.h microcode/microcode.h \
 common/string_buffer.h microcode/nova.h simulator/disk.h common/serdes.h \
 simulator/display.h simulator/ethernet.h simulator/keyboard.h \
 simulator/mouse.h common/utils.h gui/gui.h debugger/debugger.h
