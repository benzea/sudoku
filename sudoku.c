
#include "solver.h"
#include <stdio.h>

int
solved_cb(sudoku *s, int *data)
{
    *data -= 1;

    sudoku_print(s);

    /* Abort if *data is zero. */
    return (*data == 0);
}

int
main(int argc, char **argv)
{
    int solve_count = 10;
    sudoku* s = sudoku_create();

    char c;
    char n[81];

    while ((c = getchar()) != '\n') {};

    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            for (int c = 0; c < 3; c++) {
                for (int d = 0; d < 3; d++) {
                    getchar();
                    n[27 * a + 9 * b + 3 * c + d] = getchar();
                }
                if (c < 2) {
                    getchar();
                    getchar();
                }
            }
            getchar();
        }
        if (a < 2) {
            while ((c = getchar()) != '\n') {}
        }
    }

    for (int i = 0; i < 81; i++) {
        if (n[i] >= '1' && n[i] <= '9')
            sudoku_set_field(s, i / 9 + 1, i % 9 + 1, n[i] - '0');
        //putchar(n[i]);
    }

    if (sudoku_solve(s, (sudoku_solved_callback) &solved_cb, &solve_count)) {
        printf("There may be more solutions than the ones printed!\n");
    }

    return 0;
}
