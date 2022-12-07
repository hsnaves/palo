# Palo

## Synopsis

An in-progress microcode assembler and disassembler for the Xerox Alto workstation. It was reconstructed from this [memo](http://www.bitsavers.org/pdf/xerox/alto/memos_1974/Alto_Microassembler_Aug74.pdf).

## Getting started

### Prerequisites

Palo requires GCC and MAKE to be installed on the system.

### Building

To build Palo, simply type the following commands on the root directory of the Palo source code repository:

```sh
$ make
```

This will compile Palo with no debugging information and with some compiler optimizations enabled. To enable debugging information, the user can specify `DEBUG=1` in the command line above, such as:

```sh
$ DEBUG=1 OPTIMIZE=0 make
```



