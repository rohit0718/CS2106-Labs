CC=gcc
CFLAGS=-g -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS=-pthread

.PHONY: clean

all: ex2
ex2: ex2.o packer.o
clean:
	rm ex2.o packer.o ex2
