# Makefile for Subliminal Downloader (sdl)
# Compiles for host architecture, or cross-compiles for x64 / ARM.

TARGET = sdl
SRC = sdl.c

# Compilation flags and dependencies
CFLAGS = -Wall -Wextra -O3
LIBS = $(shell pkg-config --libs libcurl) -lcrypto
INCLUDES = $(shell pkg-config --cflags libcurl)

# Default native compiler
CC ?= gcc

# Cross-compilers (can be overridden by user)
CC_X64 ?= gcc
CC_ARM ?= aarch64-linux-gnu-gcc

# Output binaries for specific architectures
TARGET_X64 = $(TARGET)_x64
TARGET_ARM = $(TARGET)_arm

.PHONY: all native x64 arm clean

# Default: compile for the native host architecture
all: native

native: $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $(TARGET) $(LIBS)

# Compile specifically for x86_64 (x64)
# If cross-compiling, override CC_X64 (e.g. make x64 CC_X64=x86_64-linux-gnu-gcc)
x64: $(SRC)
	$(CC_X64) $(CFLAGS) $(INCLUDES) $(SRC) -o $(TARGET_X64) $(LIBS)

# Compile specifically for ARM (ARM64 / AArch64)
# If cross-compiling, override CC_ARM (e.g. make arm CC_ARM=aarch64-linux-gnu-gcc)
arm: $(SRC)
	$(CC_ARM) $(CFLAGS) $(INCLUDES) $(SRC) -o $(TARGET_ARM) $(LIBS)

clean:
	rm -f $(TARGET) $(TARGET_X64) $(TARGET_ARM)
