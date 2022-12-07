# makefile for palo

all: build

build:
	$(MAKE) -C src

clean:
	$(MAKE) -C src clean

.PHONY: all build clean
