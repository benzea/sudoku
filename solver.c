#if defined( __SSE__ )
#include <assert.h>
#include "solver.h"
#include "xmmintrin.h"
#include <stdint.h>
#include <stdio.h>

/* Internally an xor */
#define SET_VECTOR_ZERO(a) a = _mm_setzero_si128()
#define SET_VECTOR_ALL(a, v) a = _mm_set_epi32((v), (v), (v), (v))
//#define SET_VECTOR_ALL(a, v) while {  } do (0)
//#define SET_VECTOR_ONE(a) a = VECTOR_ALL(1)
/* OK, the setzero should not be needed, but otherwise the cmpeq is optimized away! */
#define SET_VECTOR_ONES(a, count) do { a = _mm_setzero_si128(); a = _mm_cmpeq_epi32 (a, a);  a = _mm_srli_epi32(a, 32 - count);} while (0)


#define GET_PIXEL(words, x, y) ((words[(y) / 3] >> ((x) + ((y) % 3)*9)) & 0x01)
#define SET_PIXEL(words, x, y) (words[(y) / 3] = (words[(y) / 3] | (0x01 << ((x) + ((y) % 3)*9))))

#define VEC_PRINT(vec) do { uvector __tmp; __tmp.v = vec; printf("%08x %08x %08x %08x\n", __tmp.w[0], __tmp.w[1], __tmp.w[2], __tmp.w[3]); } while (0)

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

    printf("-------------   -------------   -------------\n");
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
                if (((x % 3) == 2) && x < 8)
                    printf("   |");
            }
            printf("\n");
        }
        printf("-------------   -------------   -------------\n");
        if (((y % 3) == 2) && y < 8)
            printf("\n-------------   -------------   -------------\n");
    }
}

static inline int
update_count(sudoku* s)
{
    vector mask;
    SET_VECTOR_ONES(mask, 27);
    SET_VECTOR_ZERO(s->exactly_one_number.v);
    SET_VECTOR_ZERO(s->more_than_one_number.v);

    for (int i = 0; i < 9; i++)
    {
        __m128i overflow = _mm_and_si128(s->exactly_one_number.v, s->pages[i].v);
        s->more_than_one_number.v = _mm_or_si128(s->more_than_one_number.v, overflow);
        s->exactly_one_number.v = _mm_or_si128(s->exactly_one_number.v, s->pages[i].v);
    }

    // if there is one bit missing in s->exactly_one_number now, it's not valid anymore...
    uvector valid;
    valid.v = _mm_cmpeq_epi32(s->exactly_one_number.v, mask);
    if (!valid.w[0] || !valid.w[1] || !valid.w[2]) return 0;

    s->exactly_one_number.v = _mm_andnot_si128(s->more_than_one_number.v, s->exactly_one_number.v);
    return 1;
}

static inline void
clear_fields(sudoku* s, __m128i field_mask)
{
    for (int i = 0; i < 9; i++) {
        s->pages[i].v = _mm_andnot_si128(field_mask, s->pages[i].v);
    }
}

static inline void
set_fields(sudoku* s, __m128i field_mask, int value)
{
    clear_fields(s, field_mask);
    s->pages[value - 1].v = _mm_or_si128(field_mask, s->pages[value - 1].v);
}

// these work on the whole vector at once

// 32bit subtraction a - b
#define vec_sub(a, b) _mm_sub_epi32(a, b)

// 32bit addition a + b
#define vec_add(a, b) _mm_add_epi32(a, b)

// and a & b
#define vec_and(a, b) _mm_and_si128(a, b)

// or a | b
#define vec_or(a, b) _mm_or_si128(a, b)

// andnot (~a) & b
#define vec_andnot(a, b) _mm_andnot_si128(a, b)

// xor a ^ b
#define vec_xor(a, b) _mm_xor_si128(a, b)

// these work on each 32-bit integer seperatly 

// cmpgt a > b
#define vec_cmpgt(a, b) _mm_cmpgt_epi32(a, b)

// a >> count
#define vec_shift_right(a, count) _mm_srli_epi32(a, count)

// a << count
#define vec_shift_left(a, count) _mm_slli_epi32(a, count)

// r[0] = a[o0]; r[1] = a[o1]; r[2] = a[o2]; r[3] = a[o3]
#define vec_shuffle(a, o0, o1, o2, o3) _mm_shuffle_epi32(a, (o0) | ((o1) << 2) | ((o2) << 4) | ((o3) << 6))


static inline void
color_lines(vector *page, vector exactly_one_number) {
    vector mask;
    vector p = vec_and(*page, exactly_one_number);

    vector zero;
    vector first_line;
    vector second_line;
    vector third_line;

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(first_line, 9);
    SET_VECTOR_ONES(second_line, 18);
    SET_VECTOR_ONES(third_line, 27);
    third_line = vec_andnot(second_line, third_line);
    second_line = vec_andnot(first_line, second_line);

    // cmpgt is true, when at least one bit is set greater than any bit in the
    // second line, ie in the third line
    // and selects the third_line if cmpgt is true
    mask = vec_and(third_line, vec_cmpgt(p, second_line));
    // delete all bits from p set in mask
    p = vec_andnot(mask, p);

    // cmpgt is true, when at least one bit is set greater than any bit in the
    // first line, ie in the second line (the third line got deleted)
    mask = vec_or(mask, vec_and(second_line, vec_cmpgt(p, first_line)));
    p = vec_andnot(mask, p);

    mask = vec_or(mask, vec_and(first_line, vec_cmpgt(p, zero)));
    //mask = vec_and(first_line, vec_cmpgt(p, zero));
    mask = vec_andnot(exactly_one_number, mask);

    *page = vec_andnot(mask, *page); 
}

static inline void
color_columns(vector *page, vector exactly_one_number) {
    vector p = vec_and(*page, exactly_one_number);

    p = vec_or(p, vec_shift_left(p, 9));
    p = vec_or(p, vec_shift_left(p, 18));
    p = vec_or(p, vec_shift_right(p, 9));
    p = vec_or(p, vec_shift_right(p, 18));

    p = vec_or(p, vec_shuffle(p, 1, 2, 0, 3));
    p = vec_or(p, vec_shuffle(p, 2, 0, 1, 3));

    p = vec_andnot(exactly_one_number, p);
    *page = vec_andnot(p, *page); 

}


static inline void
color_blocks(vector *page, vector exactly_one_number) {
    uvector mask;
    vector numbers = vec_and(*page, exactly_one_number);

    #define BLOCK (7 + (7 << 9) + (7 << 18))

    vector zero;
    vector first_block;
    vector second_block;
    vector third_block;

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ALL(first_block, BLOCK);
    SET_VECTOR_ALL(second_block, BLOCK << 3);
    SET_VECTOR_ALL(third_block, BLOCK << 6);

    #undef BLOCK

    mask.v = vec_and(first_block, vec_cmpgt(vec_and(first_block, numbers), zero));
    mask.v = vec_or(mask.v, vec_and(second_block, vec_cmpgt(vec_and(second_block, numbers), zero)));
    mask.v = vec_or(mask.v, vec_and(third_block, vec_cmpgt(vec_and(third_block, numbers), zero)));

    mask.v = vec_andnot(exactly_one_number, mask.v);
    *page = vec_andnot(mask.v, *page);

}

static inline void
find_determined_line(vector *page, vector *mask) {
    // if its exactly one bit in a line, copy it into the mask
    // if there's more than one, don't
    
    vector p;
    
    vector zero;
    vector one;
    vector first_line;
    vector second_line;
    vector third_line;
    vector one_or_more_bits_set;
    vector more_than_one_bits_set;
    vector exactly_one_bit_set;

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    SET_VECTOR_ONES(first_line, 9);
    SET_VECTOR_ONES(second_line, 18);
    SET_VECTOR_ONES(third_line, 27);
    third_line = vec_andnot(second_line, third_line);
    second_line = vec_andnot(first_line, second_line);

    // third line
    p = vec_and(*page, third_line);

    one_or_more_bits_set = vec_cmpgt(p, zero);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    exactly_one_bit_set = vec_andnot(more_than_one_bits_set, one_or_more_bits_set);

    *mask = vec_and(p, exactly_one_bit_set);

    // second line
    p = vec_and(*page, second_line);

    one_or_more_bits_set = vec_cmpgt(p, zero);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    exactly_one_bit_set = vec_andnot(more_than_one_bits_set, one_or_more_bits_set);

    *mask = vec_or(*mask, vec_and(p, exactly_one_bit_set));

    // first line
    p = vec_and(*page, first_line);

    one_or_more_bits_set = vec_cmpgt(p, zero);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    exactly_one_bit_set = vec_andnot(more_than_one_bits_set, one_or_more_bits_set);

    *mask = vec_or(*mask, vec_and(p, exactly_one_bit_set));

}

// put find_determined together, they can be in one function
static inline void
find_determined_block(vector *page, vector *mask) {
    // if its exactly one bit in a block, copy it into the mask
    // if there's more than one, don't

    vector p;

    vector zero;
    vector one;
    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    vector one_or_more_bits_set;
    vector more_than_one_bits_set;
    vector exactly_one_bit_set;

    vector first_block;
    vector second_block;
    vector third_block;

    #define BLOCK (7 + (7 << 9) + (7 << 18))

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ALL(first_block, BLOCK);
    SET_VECTOR_ALL(second_block, BLOCK << 3);
    SET_VECTOR_ALL(third_block, BLOCK << 6);

    #undef BLOCK

    // third block
    p = vec_and(*page, third_block);

    one_or_more_bits_set = vec_cmpgt(p, zero);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    exactly_one_bit_set = vec_andnot(more_than_one_bits_set, one_or_more_bits_set);

    *mask = vec_and(p, exactly_one_bit_set);

    // second block
    p = vec_and(*page, second_block);

    one_or_more_bits_set = vec_cmpgt(p, zero);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    exactly_one_bit_set = vec_andnot(more_than_one_bits_set, one_or_more_bits_set);

    *mask = vec_or(*mask, vec_and(p, exactly_one_bit_set));

    // first block
    p = vec_and(*page, first_block);

    one_or_more_bits_set = vec_cmpgt(p, zero);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    exactly_one_bit_set = vec_andnot(more_than_one_bits_set, one_or_more_bits_set);

    *mask = vec_or(*mask, vec_and(p, exactly_one_bit_set));

}


static inline void
find_determined_column(vector *page, vector *mask) {
    // if its exactly one bit in a column, copy it into the mask
    // if there's more than one, don't

    vector more_than_one_bits_set;
    SET_VECTOR_ZERO(more_than_one_bits_set);
    vector one_or_more_bits_set = *page;

    vector p;

    p = vec_shift_left(*page, 9);
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_and(one_or_more_bits_set, p));
    one_or_more_bits_set = vec_or(one_or_more_bits_set, p);

    p = vec_shift_left(*page, 18);
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_and(one_or_more_bits_set, p));
    one_or_more_bits_set = vec_or(one_or_more_bits_set, p);

    p = vec_shift_right(*page, 9);
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_and(one_or_more_bits_set, p));
    one_or_more_bits_set = vec_or(one_or_more_bits_set, p);

    p = vec_shift_right(*page, 18);
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_and(one_or_more_bits_set, p));
    one_or_more_bits_set = vec_or(one_or_more_bits_set, p);

    p = vec_shuffle(one_or_more_bits_set, 1, 2, 0, 3);
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_shuffle(more_than_one_bits_set, 1, 2, 0, 3));
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_and(one_or_more_bits_set, p));
    one_or_more_bits_set = vec_or(one_or_more_bits_set, p);

    p = vec_shuffle(one_or_more_bits_set, 1, 2, 0, 3);
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_shuffle(more_than_one_bits_set, 1, 2, 0, 3));
    more_than_one_bits_set = vec_or(more_than_one_bits_set, vec_and(one_or_more_bits_set, p));
    one_or_more_bits_set = vec_or(one_or_more_bits_set, p);

    *mask = vec_and(*mask, *page);
}

static inline int
sudoku_still_valid(sudoku* s) {
    // test wether no commited numbers exclude each other

    return 1;
}


static inline int
unique_number_in_field(sudoku* s)
{
    return 0;
}

static inline int
unique_number_in_lcb(sudoku* s)
{
    return 0;
}


void
sudoku_set_all(sudoku *s)
{
    for (int i = 0; i < 9; i++) {
        SET_VECTOR_ALL(s->pages[i].v, 0x07ffffff);
        //SET_VECTOR_ALL(s->pages[i].v, 0xffffffff);
   }
}

sudoku*
sudoku_create()
{
    sudoku *res = malloc(sizeof(sudoku));

    sudoku_set_all(res);

    return res;
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

int
sudoku_solve(sudoku* s)
{
    for (long int x = 0; x < 10000; x++) {

    if (!update_count(s)) // its not valid anymore
        return 0;

    for (int i = 0; i < 9; i++) {
        color_lines(&s->pages[i].v, s->exactly_one_number.v);
        color_columns(&s->pages[i].v, s->exactly_one_number.v);
        color_blocks(&s->pages[i].v, s->exactly_one_number.v);
    }


    //color_columns(&s->pages[2].v, s->exactly_one_number.v);
    //color_blocks(&s->pages[2].v, s->exactly_one_number.v);

    vector mask;
    for (int i = 0; i < 9; i++) {
        find_determined_line(&s->pages[i].v, &mask);
        for (int j = 0; j < 9; j++) {
            if (j != i)
                s->pages[j].v = vec_andnot(mask, s->pages[j].v);
        }

        find_determined_block(&s->pages[i].v, &mask);
        for (int j = 0; j < 9; j++) {
            if (j != i)
                s->pages[j].v = vec_andnot(mask, s->pages[j].v);
        }

        find_determined_column(&s->pages[i].v, &mask);
        for (int j = 0; j < 9; j++) {
            if (j != i)
                s->pages[j].v = vec_andnot(mask, s->pages[j].v);
        }

    }
    

    }

    vector zero;
    vector one;

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    uvector p;
    vector q;

    p.v = vec_andnot((s->more_than_one_number.v - one), (s->more_than_one_number.v));
    // if there was at least one bit set, the lowest one is left, all others are unset (in each section)

    if (!(p.w[0] || p.w[1] || p.w[2])) {
        printf("Solved:\n");
        sudoku_print(s);
        printf("\n");
        return 1;
    }

    p.w[3] = 0; // is there a better way???
    q = vec_cmpgt(p.v, zero);
    q = vec_andnot(vec_shuffle(q, 3, 0, 1, 3), q);
    q = vec_andnot(vec_shuffle(q, 3, 3, 0, 3), q);
    q = vec_and(p.v, q);
    // now only the first section with one bit set is left (in q)

    sudoku* t;
    t = sudoku_copy(s);
    int mind[9];
    for (int i = 0; i < 9; i++) {
        p.v = vec_and(q, t->pages[i].v);
        mind[i] = (p.w[0] || p.w[1] || p.w[2]);
        t->pages[i].v = vec_andnot(q, t->pages[i].v);
    }

    sudoku* u;
    for (int i = 0; i < 9; i++) {
        if (mind[i]) {
            u = sudoku_copy(t);
            u->pages[i].v = vec_or(q, u->pages[i].v);
            sudoku_solve(u);
            sudoku_free(u);
        }
    }

    sudoku_free(t);



#if 0
    while (1) {
        if (!unique_number_in_field(s))
            break;

        if (!unique_number_in_lcb(s))
            break;
    }

    if (!sudoku_solved(s)) {
        sudoku backup = *s;
        __m128i field_mask = get_mask_for_one_uncomplete_field(s);

        for (int i = 0; i < 9; i++) {
            __m128i tmp = _mm_and_ps(field_mask, s->pages[i].v));

            /* Move logic into the upper 32bit */
            tmp = _mm_hadd_epi16(tmp, tmp);
            tmp = _mm_hadd_epi16(tmp, tmp);
        
            if (__mm_irgendwas_nicht_null(_mm_and_ps(field_mask, s->pages[i].v))) {
                set_fields(s, field_mask, i+1);

                if (sudoku_solve(s) {
                    return 1;
                }
                *s = backup;
            }
        }
        
        return 0;
    } else {
        return 1;
    }
#endif

    /* never reached */
    //assert(0);
    return 0;
}

void
sudoku_set_field(sudoku* s, int x, int y, int value)
{
    uvector mask;
    mask.w[0] = 0;
    mask.w[1] = 0;
    mask.w[2] = 0;
    mask.w[3] = 0;
    SET_PIXEL(mask.w, y-1, x-1);

    clear_fields(s, mask.v);
    set_fields(s, mask.v, value);
}


#else
#error No SSE extension?
#endif

