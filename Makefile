CC=clang

CFLAGS=-g -mssse -Wall -std=c99 -O2 -funroll-loops

sudoku:
	$(CC) $(CFLAGS) sudoku.c solver.c -o sudoku


.PHONY: sudoku

