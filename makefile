# Compiler and flags
CC = gcc
CFLAGS = -Wall -pthread -O2

# Target executable
TARGET = main

# Source file
SRCS = main.c

# Default target
all: $(TARGET)

# Build the target
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $<

# Clean up build files
clean:
	rm -f $(TARGET)

# Phony targets
.PHONY: all clean
