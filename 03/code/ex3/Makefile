CC=gcc
CFLAGS=-g -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS=-pthread

.PHONY: clean

all: ex3
ex3: ex3.o packer.o
clean:
	rm ex3.o packer.o ex3
