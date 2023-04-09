# Palo

## Synopsis

An in-progress microcode assembler together with an archiver tool and a simulator for the Xerox Alto workstation. The assembler was reconstructed from this [memo](http://www.bitsavers.org/pdf/xerox/alto/memos_1974/Alto_Microassembler_Aug74.pdf). The simulator was based on the [ContrAlto](https://github.com/livingcomputermuseum/ContrAlto) program.

## Getting started

### Prerequisites

Palo requires GCC, MAKE and SDL2 to be installed on the system.

### Building

To build the palo tools and simulator, simply type the following commands on the root directory of the palo source code repository:

```sh
$ make
```

This will compile palo with no debugging information and with some compiler optimizations enabled. To enable debugging information, the user can specify `DEBUG=1` in the command line above, such as:

```sh
$ DEBUG=1 OPTIMIZE=0 make
```



