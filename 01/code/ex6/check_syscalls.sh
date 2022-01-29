#!/bin/bash

####################
# Lab 1 Exercise 6
# Name:
# Student No:
# Lab Group: 
####################

echo "Printing system call report"

# Compile file
gcc -std=c99 pid_checker.c -o ex6

# Use strace to get report
strace -c ./ex6

