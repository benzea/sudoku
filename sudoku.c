
#include "solver.h"
#include <stdio.h>

int
main(int argc, char **argv)
{
    sudoku* s = sudoku_create();
    sudoku_set_field(s, 1, 1, 1);
    sudoku_set_field(s, 1, 3, 4);
    sudoku_set_field(s, 1, 5, 3);
    sudoku_set_field(s, 1, 7, 9);
    sudoku_set_field(s, 2, 2, 8);
    sudoku_set_field(s, 2, 6, 5);
    sudoku_set_field(s, 3, 4, 9);
    sudoku_set_field(s, 3, 7, 3);
    sudoku_set_field(s, 3, 9, 2);
    sudoku_set_field(s, 4, 2, 5);
    sudoku_set_field(s, 4, 3, 2);
    sudoku_set_field(s, 4, 4, 6);
    sudoku_set_field(s, 4, 9, 3);
    sudoku_set_field(s, 5, 2, 3);
    sudoku_set_field(s, 5, 8, 8);
    sudoku_set_field(s, 5, 9, 4);
    sudoku_set_field(s, 6, 1, 8);
    sudoku_set_field(s, 6, 5, 9);
    sudoku_set_field(s, 6, 8, 7);
    sudoku_set_field(s, 7, 1, 5);
    sudoku_set_field(s, 7, 2, 4);
    sudoku_set_field(s, 7, 5, 6);
    sudoku_set_field(s, 7, 6, 7);
    sudoku_set_field(s, 8, 1, 2);
    sudoku_set_field(s, 8, 7, 4);
    sudoku_set_field(s, 9, 3, 1);
    sudoku_set_field(s, 9, 6, 4);

    sudoku_print(s);

    sudoku_solve(s);

    //printf("\nSolved:\n");

    //sudoku_print(s);

    return 0;
}
