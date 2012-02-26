#if defined( __SSE__ )
#include <assert.h>
#include "solver.h"
#include "xmmintrin.h"
#include <stdint.h>
#include <stdio.h>


#define SET_TO_ZERO(a) a = _mm_xor_si128(a, a)
#define GET_PIXEL(words, x, y) ((words[y / 3] >> (x + (y % 3)*9)) & 0x01)
#define SET_PIXEL(words, x, y) (words[y / 3] = (words[y / 3] | (0x00000001 << (x + (y % 3)*9))))

typedef __m128i vector;

typedef union {
    vector v;
    uint8_t b[16];
    uint32_t w[4];
} uvector;


struct _sudoku {
    uvector pages[9];
    uvector committed;

    uvector exactly_one_number;
    uvector more_than_one_number;
};



static inline int unique_number_in_field(sudoku* s);
static inline int unique_number_in_lcb(sudoku* s);

static inline void clear_fields(sudoku* s, __m128i field_mask);
static inline void set_fields(sudoku* s, __m128i field_mask, int value);

/***************************************************/

void
sudoku_print(sudoku* s)
{
    int x, y, offset, n;

    for (n = 0; n < 9; n++) {
        printf("%08x %08x %08x %08x\n", s->pages[n].w[0], s->pages[n].w[1], s->pages[n].w[2], s->pages[n].w[3]);
    }

    printf("-------------------------------------\n");
    for (y = 0; y < 9; y++) {
        /* For every line of pages. */
        for (offset = 0; offset < 9; offset += 3) {
            /* each line of pages has three lines */
            printf("|");
            for (x = 0; x < 9; x++) {
                /* Each number on the line */
                for (n = offset; n < offset + 3; n++) {
                    if (GET_PIXEL(s->pages[n].w, x, y)) {
                        printf("%i", n+1);
                    } else {
                        printf(" ");
                    }
                }
                printf("|");
            }
            printf("\n");
        }
        printf("-------------------------------------\n");
    }
}

static inline void
update_count(sudoku* s)
{
    SET_TO_ZERO(s->exactly_one_number.v);
    SET_TO_ZERO(s->more_than_one_number.v);

    for (int i = 0; i < 9; i++)
    {
        __m128i tmp = _mm_and_si128(s->exactly_one_number.v, s->pages[i].v);
        s->more_than_one_number.v = _mm_or_si128(s->more_than_one_number.v, tmp);
        s->exactly_one_number.v = _mm_or_si128(s->exactly_one_number.v, s->pages[i].v);
        s->exactly_one_number.v = _mm_andnot_si128(s->more_than_one_number.v, s->exactly_one_number.v);
    }
}

static inline void
clear_fields(sudoku* s, __m128i field_mask)
{
    for (int i = 0; i < 9; i++) {
        s->pages[i].v = _mm_andnot_ps(field_mask, s->pages[i].v);
    }
}

static inline void
set_fields(sudoku* s, __m128i field_mask, int value)
{
    clear_fields(s, field_mask);
    s->pages[value - 1].v = _mm_or_ps(field_mask, s->pages[value - 1].v);
}

void
sudoku_set_field(sudoku* s, int x, int y, int value)
{
    SET_PIXEL(s->pages[value-1].w, x, y);
}


static inline int
unique_number_in_field(sudoku* s)
{
    
}

static inline int
unique_number_in_lcb(sudoku* s)
{
}


sudoku*
sudoku_create()
{
    return malloc(sizeof(sudoku));
}

sudoku*
sudoku_copy(sudoku* s)
{
    sudoku* res = sudoku_create();
    *res = *s;

    return res;
}

void
sudoku_free(sudoku* s)
{
    free(s);
}



#else
#error No SSE extension?
#endif

