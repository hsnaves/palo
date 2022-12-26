# makefile for building palo

# Set the defaults

ifndef OPTIMIZE
    OPTIMIZE := 1
endif

ifndef DEBUG
    DEBUG := 0
endif

# General definitions

CC := gcc
RM := rm -f

CFLAGS := -Wall -Wextra -ansi -pedantic -std=c99 \
          $(shell sdl2-config --cflags) $(EXTRA_CFLAGS)
LDFLAGS := $(shell sdl2-config --libs)

INCLUDES := -I.
LIBS :=

TARGET := pmu palos

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

palos: $(PALOS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	$(RM) $(TARGET) $(OBJS)

.PHONY: all clean
