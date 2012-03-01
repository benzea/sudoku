
#include "solver.h"
#include <stdio.h>

int
main(int argc, char **argv)
{
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

    sudoku_print(s);

    sudoku_solve(s);

    return 0;
}
