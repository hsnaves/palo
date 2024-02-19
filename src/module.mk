ASSEMBLER_OBJS := assembler/assembler.o assembler/objfile.o
COMMON_OBJS := common/allocator.o common/table.o common/serdes.o \
 common/string_buffer.o common/utils.o
DEBUGGER_OBJS := debugger/debugger.o debugger/cmd.o
FS_OBJS := fs/basic.o fs/check.o fs/dir.o fs/disk.o fs/file.o fs/fs.o \
 fs/meta.o fs/scan.o fs/print.o
GUI_OBJS := gui/gui.o gui/udp_transport.o
MICROCODE_OBJS := microcode/microcode.o microcode/nova.o
PARSER_OBJS := parser/parser.o parser/lexer.o
SIMULATOR_OBJS := simulator/simulator.o simulator/disk.o \
 simulator/display.o simulator/ethernet.o simulator/keyboard.o \
 simulator/mouse.o simulator/intr.o simulator/rom.o


PMU_OBJS := $(ASSEMBLER_OBJS) $(COMMON_OBJS) $(PARSER_OBJS) \
 microcode/microcode.o pmu.o
PAR_OBJS := $(FS_OBJS) common/utils.o par.o
PALOS_OBJS := $(COMMON_OBJS) $(DEBUGGER_OBJS) $(GUI_OBJS) $(MICROCODE_OBJS) \
 $(SIMULATOR_OBJS) assembler/objfile.o palos.o
OBJS := $(ASSEMBLER_OBJS) $(COMMON_OBJS) $(DEBUGGER_OBJS) $(FS_OBJS) \
 $(GUI_OBJS) $(MICROCODE_OBJS) $(PARSER_OBJS) $(SIMULATOR_OBJS) \
 pmu.o par.o palos.o


assembler/assembler.o: assembler/assembler.c assembler/assembler.h \
 assembler/objfile.h common/allocator.h common/serdes.h \
 common/string_buffer.h common/table.h common/utils.h \
 microcode/microcode.h parser/lexer.h parser/parser.h
assembler/objfile.o: assembler/objfile.c assembler/objfile.h \
 common/allocator.h common/serdes.h common/string_buffer.h  \
 common/table.h common/utils.h microcode/microcode.h
common/allocator.o: common/allocator.c common/allocator.h common/utils.h
common/serdes.o: common/serdes.c common/serdes.h common/utils.h
common/string_buffer.o: common/string_buffer.c common/string_buffer.h \
 common/utils.h
common/table.o: common/table.c common/table.h common/utils.h
common/utils.o: common/utils.c common/utils.h
debugger/cmd.o: debugger/cmd.c assembler/objfile.h common/allocator.h \
 common/serdes.h common/string_buffer.h common/table.h common/utils.h \
 debugger/debugger.h gui/gui.h microcode/microcode.h  microcode/nova.h \
 simulator/display.h simulator/disk.h simulator/ethernet.h simulator/intr.h \
  simulator/keyboard.h simulator/mouse.h simulator/simulator.h
debugger/debugger.o: debugger/debugger.c assembler/objfile.h \
 common/allocator.h common/serdes.h common/string_buffer.h common/table.h \
 common/utils.h debugger/debugger.h gui/gui.h microcode/microcode.h \
 microcode/nova.h simulator/display.h simulator/disk.h  simulator/ethernet.h \
 simulator/keyboard.h simulator/mouse.h simulator/simulator.h
gui/gui.o: gui/gui.c common/serdes.h common/string_buffer.h common/utils.h \
 gui/gui.h microcode/microcode.h microcode/nova.h simulator/display.h \
 simulator/disk.h simulator/ethernet.h simulator/keyboard.h simulator/mouse.h \
 simulator/simulator.h
gui/udp_transport.o: gui/udp_transport.c common/serdes.h \
 common/string_buffer.h common/utils.h gui/udp_transport.h \
 microcode/microcode.h simulator/ethernet.h
fs/basic.o: fs/basic.c common/utils.h fs/fs.h fs/fs_internal.h
fs/check.o: fs/check.c common/utils.h fs/fs.h fs/fs_internal.h
fs/dir.o: fs/dir.c common/utils.h fs/fs.h fs/fs_internal.h
fs/disk.o: fs/disk.c common/utils.h fs/fs.h fs/fs_internal.h
fs/file.o: fs/file.c common/utils.h fs/fs.h fs/fs_internal.h
fs/fs.o: fs/fs.c common/utils.h fs/fs.h fs/fs_internal.h
fs/meta.o: fs/meta.c common/utils.h fs/fs.h fs/fs_internal.h
fs/print.o: fs/print.c common/utils.h fs/fs.h
fs/scan.o: fs/scan.c common/utils.h fs/fs.h fs/fs_internal.h
microcode/microcode.o: microcode/microcode.c common/string_buffer.h \
 common/utils.h microcode/microcode.h
microcode/nova.o: microcode/nova.c common/string_buffer.h common/utils.h \
 microcode/microcode.h microcode/nova.h
parser/lexer.o: parser/lexer.c common/allocator.h  common/table.h \
 common/utils.h parser/lexer.h
parser/parser.o: parser/parser.c common/allocator.h common/table.h \
 common/utils.h parser/lexer.h parser/parser.h
simulator/disk.o: simulator/disk.c common/serdes.h common/string_buffer.h \
 common/utils.h microcode/microcode.h simulator/disk.h simulator/intr.h
simulator/display.o: simulator/display.c common/serdes.h \
 common/string_buffer.h common/utils.h microcode/microcode.h \
 simulator/display.h simulator/intr.h
simulator/ethernet.o: simulator/ethernet.c common/serdes.h \
 common/string_buffer.h common/utils.h microcode/microcode.h \
 simulator/ethernet.h simulator/intr.h
simulator/intr.o: simulator/intr.c common/utils.h simulator/intr.h
simulator/keyboard.o: simulator/keyboard.c common/serdes.h \
 common/string_buffer.h common/utils.h microcode/microcode.h \
 simulator/keyboard.h
simulator/mouse.o: simulator/mouse.c common/serdes.h common/string_buffer.h \
 common/utils.h microcode/microcode.h simulator/mouse.h
simulator/rom.o: simulator/rom.c common/string_buffer.h \
 common/string_buffer.h microcode/microcode.h simulator/rom.h
simulator/simulator.o: simulator/simulator.c common/serdes.h \
 common/string_buffer.h common/utils.h microcode/microcode.h microcode/nova.h \
 simulator/display.h simulator/disk.h simulator/ethernet.h simulator/intr.h \
 simulator/keyboard.h simulator/mouse.h simulator/rom.h simulator/simulator.h
palos.o: palos.c assembler/objfile.h common/allocator.h common/serdes.h \
 common/string_buffer.h common/table.h common/utils.h debugger/debugger.h \
 gui/gui.h gui/udp_transport.h microcode/microcode.h microcode/nova.h \
 simulator/display.h simulator/disk.h simulator/ethernet.h \
 simulator/keyboard.h simulator/mouse.h simulator/simulator.h
par.o: par.c common/utils.h fs/fs.h
pmu.o: pmu.c assembler/assembler.h assembler/objfile.h common/allocator.h \
 common/serdes.h common/string_buffer.h common/table.h common/utils.h \
 microcode/microcode.h parser/parser.h parser/lexer.h \
 parser/parser.h
