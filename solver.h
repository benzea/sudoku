

typedef struct _sudoku sudoku;

sudoku* sudoku_create();
void sudoku_set_all(sudoku *s);
sudoku* sudoku_copy(sudoku* s);
void sudoku_free(sudoku* s);
void sudoku_print(sudoku* s);

int sudoku_solve(sudoku* s);

void sudoku_set_field(sudoku* s, int x, int y, int value);
int sudoku_get_field(sudoku* s, int x, int y);

//void sudoku_get_possibilities(int x, int y, bool* list);

