/* main.c — starter file for Challenge 005
 *
 * Compile: gcc -g -O0 -Wall -Wextra -o main main.c
 * Debug:   gdb ./main
 *            (gdb) source value_printer.py
 *            (gdb) break inspect
 *            (gdb) run
 *            (gdb) p v
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { VAL_INT = 0, VAL_FLOAT = 1, VAL_STRING = 2 } ValueKind;

typedef struct {
    ValueKind kind;
    union {
        int64_t  as_int;
        double   as_float;
        struct {
            const char *ptr;
            size_t      len;
        } as_str;
    };
} Value;

static Value make_int(int64_t n)
{
    Value v;
    v.kind   = VAL_INT;
    v.as_int = n;
    return v;
}

static Value make_float(double d)
{
    Value v;
    v.kind     = VAL_FLOAT;
    v.as_float = d;
    return v;
}

static Value make_str(const char *s)
{
    Value v;
    v.kind        = VAL_STRING;
    v.as_str.ptr  = s;
    v.as_str.len  = strlen(s);
    return v;
}

/* Set a breakpoint on this function and inspect `v` with `p v`. */
void inspect(Value v)
{
    (void)v;
}

int main(void)
{
    inspect(make_int(42));
    inspect(make_float(3.14));
    inspect(make_str("hello"));
    return 0;
}
