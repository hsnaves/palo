# makefile for palo

all: build

build:
	$(MAKE) -C src

install:
	$(MAKE) -C src install

clean:
	$(MAKE) -C src clean

.PHONY: all build install clean
