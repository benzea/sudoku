
#include "solver.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

typedef struct {
    int count;
    int max_count;
} solver;

int
solved_cb(sudoku *s, solver *data)
{
    data->count += 1;

    printf("Solution number %i:\n", data->count);
    sudoku_print(s);
    printf("\n");

    /* Abort if the maximum count has been reached. */
    return (data->count == data->max_count);
}

int
main(int argc, char **argv)
{
    solver state;
    sudoku* s = sudoku_create();

    state.max_count = 1;
    state.count = 0;

    int c;
    char n[81];

    while ((c = getopt (argc, argv, "c:")) != -1) {
        char *end = NULL;
        switch (c) {
            case 'c':
                state.max_count = strtol(optarg, &end, 10);
                if (*end != '\0') {
                    fprintf (stderr, "Option -c requires an integer argument.\n");
                    return 1;
                }
                if (state.max_count < 0) {
                    fprintf (stderr, "Argument for option -c should be larger than zero.\n");
                    return 1;
                }
                break;
            case '?':
                if (optopt == 'c')
                    fprintf (stderr, "Option -%c requires an integer argument.\n", optopt);
                else if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                             "Unknown option character `\\x%x'.\n",
                             optopt);

                return 1;
        }
    }

    do { c = getchar(); if (c == -1) return 1;} while (c != '\n');

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
            do { c = getchar(); if (c == -1) return 1;} while (c != '\n');
        }
    }

    for (int i = 0; i < 81; i++) {
        if (n[i] >= '1' && n[i] <= '9')
            sudoku_set_field(s, i / 9 + 1, i % 9 + 1, n[i] - '0');
        //putchar(n[i]);
    }

    if (sudoku_solve(s, (sudoku_solved_callback) &solved_cb, &state)) {
        printf("There may be more solutions than the ones printed!\n");
    }

    return 0;
}
