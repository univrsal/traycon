# Platform detection
UNAME := $(shell uname -s)

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O2 -std=c99

ifeq ($(UNAME),Linux)
  DBUS_CFLAGS := $(shell pkg-config --cflags dbus-1)
  DBUS_LIBS   := $(shell pkg-config --libs   dbus-1)
  PLATFORM_CFLAGS = $(DBUS_CFLAGS)
  PLATFORM_LIBS   = $(DBUS_LIBS)
else ifeq ($(UNAME),Darwin)
  # Compile as Objective-C so the AppKit implementation in the bundled
  # header is accepted by the compiler.
  PLATFORM_CFLAGS = -x objective-c
  PLATFORM_LIBS   = -framework Cocoa
else ifeq ($(OS),Windows_NT)
  PLATFORM_CFLAGS =
  PLATFORM_LIBS   = -lshell32 -luser32 -lgdi32
endif

.PHONY: all bundle clean

all: example

# Regenerate the single-file header from sources
bundle traycon.h:
	python3 bundle.py

example: example.c traycon.h
	$(CC) $(CFLAGS) $(PLATFORM_CFLAGS) -I. -o $@ example.c $(PLATFORM_LIBS) -lm

clean:
	rm -f example traycon.h
