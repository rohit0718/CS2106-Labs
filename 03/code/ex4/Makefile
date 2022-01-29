CC=gcc
CFLAGS=-g -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS=-pthread

.PHONY: clean

all: ex4
ex4: ex4.o restaurant.o
clean:
	rm ex4.o restaurant.o ex4
