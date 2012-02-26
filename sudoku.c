
#include "solver.h"
#include <stdio.h>

int
main(int argc, char **argv)
{
    sudoku* s = sudoku_create();
    sudoku_set_field(s, 1, 2, 3);
    sudoku_set_field(s, 2, 1, 1);
    sudoku_set_field(s, 3, 1, 2);
    sudoku_set_field(s, 5, 1, 3);
    sudoku_print(s);

    sudoku_solve(s);

    printf("\nSolved:\n");

    sudoku_print(s);

    return 0;
}
