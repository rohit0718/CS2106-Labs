CC=gcc
CFLAGS=-g -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

.PHONY: clean

all: myshell
myshell: myshell.o driver.o
clean:
	rm myshell.o driver.o myshell
