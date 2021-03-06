#if defined( __SSE__ )
#include <assert.h>
#include "solver.h"
#include "xmmintrin.h"
#include <stdint.h>
#include <stdio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Internally an xor */
#define SET_VECTOR_ZERO(a) a = _mm_setzero_si128()
#define SET_VECTOR_ALL(a, v) a = _mm_set1_epi32((v))
//#define SET_VECTOR_ALL(a, v) while {  } do (0)
//#define SET_VECTOR_ONE(a) a = VECTOR_ALL(1)
/* OK, the setzero should not be needed, but otherwise the cmpeq is optimized away! */
#define SET_VECTOR_ONES(a, count) do { a = _mm_setzero_si128(); a = _mm_cmpeq_epi32 (a, a);  if (count < 32) a = _mm_srli_epi32(a, 32 - count);} while (0)

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

// a == b on 32 bit words
#define vec_eq(a, b) _mm_cmpeq_epi32(a, b)

// These need to have a "logical" value in the vector, ie. all ones in each word.
#define vec_sudoku_any_true(a) ((_mm_movemask_epi8(a) & 0x0fff))
#define vec_sudoku_all_true(a) ((_mm_movemask_epi8(a) & 0x0fff) == 0x0fff)
#define vec_sudoku_all_false(a) ((_mm_movemask_epi8(a) & 0x0fff) == 0x0000)

// Usefull constant vector definitions
#define BLOCK_CONSTANTS vector first_block; vector second_block; vector third_block; SET_VECTOR_ALL(first_block, (7 + (7 << 9) + (7 << 18))); second_block = vec_shift_left(first_block, 3); third_block = vec_shift_left(first_block, 6); 
#define LINE_CONSTANTS vector first_line; vector second_line; vector third_line; SET_VECTOR_ONES(first_line, 9); second_line = vec_shift_left(first_line, 9); third_line = vec_shift_left(first_line, 18);


#define GET_PIXEL(v, x, y) (_mm_movemask_epi8(vec_shift_left(v, 31 - ((x) + 9*((y) % 3)))) & (1 << ((((y) / 3)*4)+3)))
/* SET_PIXEL is not very nice ... someone has a good idea? */
#define SET_PIXEL(v, x, y) do { \
    int bit_pos = (x) + ((y) % 3) * 9; \
    vector tmp; \
    tmp = _mm_cvtsi32_si128(1 << bit_pos); \
    if (((y) / 3) == 1) { \
        tmp = vec_shuffle(tmp, 1, 0, 1, 1); \
    } else if (((y) / 3) == 2) { \
        tmp = vec_shuffle(tmp, 1, 1, 0, 1); \
    } \
    v = vec_or(v, tmp); \
} while (0)

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


static inline int update_count(sudoku* s);
static inline int unique_number_in_field(sudoku* s);
static inline int unique_number_in_lcb(sudoku* s);

static inline void clear_fields(sudoku* s, __m128i field_mask);
static inline void set_fields(sudoku* s, __m128i field_mask, int value);

/***************************************************/

void
sudoku_print(sudoku* s)
{
    int x, y, n;

    update_count(s);

    for (y = 0; y < 9; y++) {
        /* For every line of pages. */
        if ((y > 0) && (y % 3 == 0))
            printf("-------+-------+-------\n");

        for (x = 0; x < 9; x++) {
            if ((x > 0) && (x % 3 == 0))
                printf(" |");

            /* Put a '.' into the field, if there are multiple possibilities */
            printf(" ");
            if (GET_PIXEL(s->more_than_one_number.v, x, y)) {
                printf(".");
            } else if (GET_PIXEL(s->exactly_one_number.v, x, y)) {
                for (n = 0; n < 9; n++) {
                    if (GET_PIXEL(s->pages[n].v, x, y))
                        printf("%i", n + 1);
                }
            } else {
                /* Impossible to fit anything. */
                printf("X");
            }
        }
        printf("\n");
    }
}


void
sudoku_print_full(sudoku* s)
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
                    if (GET_PIXEL(s->pages[n].v, x, y)) {
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
    vector valid;
    valid = _mm_cmpeq_epi32(s->exactly_one_number.v, mask);
    if (!vec_sudoku_all_true(valid))
        return 0;

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

static inline void
color_lines(vector *page, vector exactly_one_number) {
    vector mask;
    vector p = vec_and(*page, exactly_one_number);

    vector zero;
    LINE_CONSTANTS

    SET_VECTOR_ZERO(zero);

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
    vector mask;
    vector numbers = vec_and(*page, exactly_one_number);

    vector zero;
    BLOCK_CONSTANTS

    SET_VECTOR_ZERO(zero);


    mask = vec_and(first_block, vec_cmpgt(vec_and(first_block, numbers), zero));
    mask = vec_or(mask, vec_and(second_block, vec_cmpgt(vec_and(second_block, numbers), zero)));
    mask = vec_or(mask, vec_and(third_block, vec_cmpgt(vec_and(third_block, numbers), zero)));

    mask = vec_andnot(exactly_one_number, mask);
    *page = vec_andnot(mask, *page);

}

static inline void
find_determined_line(vector *page, vector *mask) {
    // if its exactly one bit in a line, copy it into the mask
    // if there's more than one, don't
    
    vector p;
    
    vector zero;
    vector one;
    LINE_CONSTANTS
    vector one_or_more_bits_set;
    vector more_than_one_bits_set;
    vector exactly_one_bit_set;

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

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

    BLOCK_CONSTANTS

    SET_VECTOR_ZERO(zero);

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

static inline void
valid_block(vector page, vector *valid)
{
    vector p;

    vector zero;
    vector one;
    BLOCK_CONSTANTS

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    vector more_than_one_bits_set;

    p = vec_and(page, first_block);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);

    p = vec_and(page, second_block);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);

    p = vec_and(page, third_block);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);
}

static inline void
valid_line(vector page, vector *valid)
{
    vector p;

    vector zero;
    vector one;
    LINE_CONSTANTS

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    vector more_than_one_bits_set;

    p = vec_and(page, first_line);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);

    p = vec_and(page, second_line);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);

    p = vec_and(page, third_line);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);
}

static inline void
valid_column(vector page, vector *valid)
{
    /* Transform the colums to blocks, then check those.
     * Note that the blocks are shifted. The first word is normal, the second
     * is shifted by one bit, the third is shifted by two bits to the left. */

    vector shift_0;
    vector shift_1;
    vector shift_2;

    #define TMP ((1 << 0) + (1 << 3) + (1 << 6) + (1 << 9))
    #define BLK_LINE ((TMP << 0) | (TMP << 9) | (TMP << 18))
    vector column_mask = _mm_set_epi32(0, BLK_LINE << 2, BLK_LINE << 1, BLK_LINE << 0);
    #undef TMP
    #undef BLK_LINE

    shift_0 = vec_and(page, column_mask);
    shift_1 = vec_and(page, vec_shift_right(column_mask, 1));
    shift_2 = vec_and(page, vec_shift_right(column_mask, 2));

    shift_1 = vec_shuffle(shift_1, 1, 2, 0, 3);
    shift_2 = vec_shuffle(shift_2, 2, 0, 1, 3);

    //shift_0 = vec_shift_left(shift_0, 0);
    shift_1 = vec_shift_left(shift_1, 1);
    shift_2 = vec_shift_left(shift_2, 2);

    page = vec_or(shift_0, vec_or(shift_1, shift_2));

    vector zero;
    vector one;
    vector first_block;
    vector second_block;
    vector third_block;

    #define BLOCK (7 + (7 << 9) + (7 << 18))
    /* We use shifted blocks in the words. Note the endianness ... */
    first_block = _mm_set_epi32(0, BLOCK << 2, BLOCK << 1, BLOCK);
    #undef BLOCK
    second_block = vec_shift_left(first_block, 3);
    third_block = vec_shift_left(first_block, 6);

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    vector p;
    vector more_than_one_bits_set;

    p = vec_and(page, first_block);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);

    p = vec_and(page, second_block);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);

    p = vec_and(page, third_block);
    more_than_one_bits_set = vec_cmpgt(vec_and(p, vec_sub(p, one)), zero);
    *valid = vec_andnot(more_than_one_bits_set, *valid);
}

static inline int
check_valid(sudoku* s) {
    // test wether no numbers exclude each other

    vector valid;

    SET_VECTOR_ONES(valid, 32);

    for (int i = 0; i < 9; i++) {
        vector masked_page;

        masked_page = vec_and(s->pages[i].v, s->exactly_one_number.v);
        valid_block(masked_page, &valid);
        valid_line(masked_page, &valid);
        valid_column(masked_page, &valid);
    }

    return vec_sudoku_all_true(valid);
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

        /* Set the last word to all zero */
        s->pages[i].v = _mm_insert_epi16(s->pages[i].v, 0, 6);
        s->pages[i].v = _mm_insert_epi16(s->pages[i].v, 0, 7);
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
sudoku_solve(sudoku* s, sudoku_solved_callback cb, void *data)
{
    int changed;

    if (!update_count(s)) // its not valid anymore
        return 0;

    do {
        /* no_changes is all 1 if there was a change, and all zero if there was none.
         * So and'ing it with a number, results the number or zero.
         * Note: This works because every block needs to contain each number
         *       more than one time */
        vector no_changes;
        changed = 0;

        SET_VECTOR_ONES(no_changes, 32);

        for (int i = 0; i < 9; i++) {
            no_changes = vec_and(no_changes, s->pages[i].v);

            color_lines(&s->pages[i].v, s->exactly_one_number.v);
            color_columns(&s->pages[i].v, s->exactly_one_number.v);
            color_blocks(&s->pages[i].v, s->exactly_one_number.v);

            no_changes = vec_eq(no_changes, s->pages[i].v);
        }

        vector mask;
        for (int i = 0; i < 9; i++) {
            no_changes = vec_and(no_changes, s->pages[i].v);

            find_determined_line(&s->pages[i].v, &mask);
            for (int j = 0; j < 9; j++) {
                if (j != i) {
                    s->pages[j].v = vec_andnot(mask, s->pages[j].v);
                }
            }

            find_determined_block(&s->pages[i].v, &mask);
            for (int j = 0; j < 9; j++) {
                if (j != i) {
                    s->pages[j].v = vec_andnot(mask, s->pages[j].v);
                }
            }

            find_determined_column(&s->pages[i].v, &mask);
            for (int j = 0; j < 9; j++) {
                if (j != i) {
                    s->pages[j].v = vec_andnot(mask, s->pages[j].v);
                }
            }

            no_changes = vec_eq(no_changes, s->pages[i].v);
        }

        if (!update_count(s)) // its not valid anymore
            return 0;

        if (!check_valid(s))
            return 0;

        changed = !vec_sudoku_all_true(no_changes);

    } while (changed);

    vector zero;
    vector one;

    SET_VECTOR_ZERO(zero);
    SET_VECTOR_ONES(one, 1);

    vector p;
    vector q;

    /* It is guaranteed, that there is no empty field at this point. */

    p = vec_andnot((s->more_than_one_number.v - one), (s->more_than_one_number.v));
    // if there was at least one bit set, the lowest one is left, all others are unset (in each section)

    /* Check whether there is no bit set anywhere. */
    q = vec_cmpgt(p, zero);
    if (vec_sudoku_all_false(q)) {
        return cb(s, data);
    }

    /* We assume that the highest word is 0 here (see sudoku_set_all) */
    //q = vec_cmpgt(p.v, zero); already done above
    q = vec_andnot(vec_shuffle(q, 3, 0, 1, 3), q);
    q = vec_andnot(vec_shuffle(q, 3, 3, 0, 3), q);
    q = vec_and(p, q);
    // now only the first section with one bit set is left (in q)

    sudoku* t;
    t = sudoku_copy(s);
    int mind[9];
    for (int i = 0; i < 9; i++) {
        p = vec_and(q, t->pages[i].v);
        mind[i] = vec_sudoku_any_true(vec_cmpgt(p, zero));

        /* Clear all pages where the bit is set */
        t->pages[i].v = vec_andnot(q, t->pages[i].v);
    }

    sudoku* u;
    for (int i = 0; i < 9; i++) {
        if (mind[i]) {
            int abort;
            u = sudoku_copy(t);

            /* Set one of the numbers again */
            u->pages[i].v = vec_or(q, u->pages[i].v);
            abort = sudoku_solve(u, cb, data);
            sudoku_free(u);

            if (abort) {
                return abort;
            }
        }
    }

    sudoku_free(t);

    return 0;
}

void
sudoku_set_field(sudoku* s, int x, int y, int value)
{
    vector mask;

    x = MIN(9, MAX(1, x));
    y = MIN(9, MAX(1, y));
    value = MIN(9, MAX(1, value));

    SET_VECTOR_ZERO(mask);
    SET_PIXEL(mask, y-1, x-1);

    clear_fields(s, mask);
    set_fields(s, mask, value);
}


#else
#error No SSE extension?
#endif

