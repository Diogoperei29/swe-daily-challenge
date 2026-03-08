#include <stdio.h>
#include <assert.h>
#include "arena.h"

int main() {
    Arena *a = arena_create(4096);

    #define N 8
    struct { void *ptr; size_t size; } allocs[N];
    size_t sizes[N]  = {1, 3, 7, 16, 4, 32, 8, 5};
    size_t aligns[N] = {1, 4, 8, 16, 1, 64, 8, 4};

    void *first_base = a->base;

    for (int cycle = 0; cycle < 3; cycle++) {
        // allocate
        for (int i = 0; i < N; i++) {
            allocs[i].ptr  = arena_alloc(a, sizes[i], aligns[i]);
            allocs[i].size = sizes[i];
            assert(allocs[i].ptr != NULL);
            assert((uintptr_t)allocs[i].ptr % aligns[i] == 0);
        }

        // verify no overlaps
        int overlaps = 0;
        for (int i = 0; i < N; i++)
            for (int j = i + 1; j < N; j++) {
                uintptr_t ai = (uintptr_t)allocs[i].ptr;
                uintptr_t aj = (uintptr_t)allocs[j].ptr;
                if (!(ai + allocs[i].size <= aj || aj + allocs[j].size <= ai))
                    overlaps++;
            }

        printf("Cycle %d: %d allocs, %d overlaps, base %p\n", cycle, N, overlaps, a->base);
        assert(a->base == first_base);

        arena_reset(a);
        assert(a->base == first_base);
    }

    arena_destroy(a);
}