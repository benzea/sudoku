

typedef struct _sudoku sudoku;

typedef int (*sudoku_solved_callback) (sudoku *, void *);

sudoku* sudoku_create();
void sudoku_set_all(sudoku *s);
sudoku* sudoku_copy(sudoku* s);
void sudoku_free(sudoku* s);
void sudoku_print(sudoku* s);
void sudoku_print_full(sudoku* s);

int sudoku_solve(sudoku* s, sudoku_solved_callback cb, void *data);

void sudoku_set_field(sudoku* s, int x, int y, int value);
int sudoku_get_field(sudoku* s, int x, int y);

//void sudoku_get_possibilities(int x, int y, bool* list);

