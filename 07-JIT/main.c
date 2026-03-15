// compile: gcc -Wall -Wextra -o jit main.c


#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define JIT_SIZE 4096

typedef int  (*add_fn)(int,  int);
typedef int  (*mul_fn)(int,  int);
typedef long (*fib_fn)(long);

int main(void)
{
    uint8_t *code = mmap(NULL, JIT_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);

    if (code == MAP_FAILED) { perror("mmap"); exit(1); }

    
    size_t off = 0;

    /* ------------------------------------------------------------------ */
    /* add(int a, int b) — System V AMD64: a→edi, b→esi, return→eax       */
    /* ------------------------------------------------------------------ */
    size_t add_off = off;
    
    // MOV eax, edi
    code[off++] = 0x89; // MOV = 0x89
    code[off++] = 0xF8; // MOV r/m32,r32: reg=src=edi(7), rm=dst=eax(0) ModRM: (3<<6)|(7<<3)|0 = 0xF8
    // ADD eax, esi     
    code[off++] = 0x01; // ADD = 0x01
    code[off++] = 0xF0; // ADD r/m32,r32: reg=src=esi(6), rm=dst=eax(0) ModRM: (3<<6)|(6<<3)|0 = 0xF0
    // RET
    code[off++] = 0xC3; // RET = 0xC3

    /* ------------------------------------------------------------------ */
    /* mul(int a, int b) — same calling convention                        */
    /* ------------------------------------------------------------------ */
    size_t mul_off = off;

    // MOV eax, edi  (load first argument into eax)
    code[off++] = 0x89; // MOV = 0x89
    code[off++] = 0xF8; // MOV r/m32,r32: reg=src=edi(7), rm=dst=eax(0) ModRM: (3<<6)|(7<<3)|0 = 0xF8
    // IMUL eax, esi  (eax = eax * esi)
    code[off++] = 0x0F; // IMUL = 0x0F 0xAF
    code[off++] = 0xAF;
    code[off++] = 0xC6; // IMUL r32,r/m32: reg=dst=eax(0), rm=src=esi(6) ModRM: (3<<6)|(0<<3)|6 = 0xC6
    // RET
    code[off++] = 0xC3; // RET = 0xC3

    if (mprotect(code, JIT_SIZE, PROT_READ | PROT_EXEC) == -1) {
        perror("mprotect"); exit(1);
    }
    /* ------------------------------------------------------------------ */
    /* Call the JIT-compiled functions                                      */
    /* ------------------------------------------------------------------ */
    add_fn add = (add_fn)(void *)(code + add_off);
    mul_fn mul = (mul_fn)(void *)(code + mul_off);

    int r_add = add(3, 4);
    int r_mul = mul(6, 7);

    printf("add(3,  4)  = %d   [%s]\n", r_add, r_add == 7  ? "OK" : "FAIL");
    printf("mul(6,  7)  = %d  [%s]\n",  r_mul, r_mul == 42 ? "OK" : "FAIL");

    /* ------------------------------------------------------------------ */
    /* STRETCH: fibonacci(long n) — iterative, 64-bit registers            */
    /* ------------------------------------------------------------------ */
#ifdef STRETCH
    /* TODO 5 (stretch): Re-allocate (or extend) the region and JIT-compile
     *         an iterative fibonacci function using 64-bit instructions.
     *         Remember: REX.W prefix (0x48) is required for 64-bit operands.
     *         Use the patch-displacement technique from CHALLENGE.md for
     *         the forward conditional jump past the base-case. */
    (void)fib_fn; /* suppress unused-typedef warning until implemented */
#endif

    munmap(code, JIT_SIZE);
    return (r_add == 7 && r_mul == 42) ? 0 : 1;
}
