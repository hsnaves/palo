# makefile for building palo

# Set the defaults

ifndef OPTIMIZE
    OPTIMIZE := 1
endif

ifndef DEBUG
    DEBUG := 0
endif

# PREFIX is environment variable, but if it is not set, then set default value
ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

# General definitions

CC := gcc
INSTALL := install
RM := rm -f

CFLAGS := -Wall -Wextra -ansi -pedantic -std=c99 \
          $(shell sdl2-config --cflags) $(EXTRA_CFLAGS)
LDFLAGS := $(shell sdl2-config --libs)

INCLUDES := -I.
LIBS :=

TARGET := pmu par palos

# Modify the FLAGS based on the options

ifneq ($(OPTIMIZE), 0)
    # Maybe add -fno-math-errno
    CFLAGS := $(CFLAGS) -O3
else
    CFLAGS := $(CFLAGS) -O0
endif

ifneq ($(DEBUG), 0)
    CFLAGS := $(CFLAGS) -g -ggdb
    LDFLAGS := $(LDFLAGS) -g
endif

# Main targets

all: $(TARGET)

include module.mk

# Pattern rules

pmu: $(PMU_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

par: $(PAR_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

palos: $(PALOS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -m 755 pmu $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -m 755 par $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -m 755 palos $(DESTDIR)$(PREFIX)/bin/

clean:
	$(RM) $(TARGET) $(OBJS)

.PHONY: all install clean
