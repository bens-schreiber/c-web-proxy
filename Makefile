# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -std=c11 -DLOGGING=1 -D_POSIX_C_SOURCE=200112L -g

# Linker flags (REQUIRED FOR LINUX)
LDFLAGS = -lnsl

# Source files
SRCS = src/main.c src/proxy.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executable name
TARGET = proxy_server

# Default target
all: $(TARGET)

# Link object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
