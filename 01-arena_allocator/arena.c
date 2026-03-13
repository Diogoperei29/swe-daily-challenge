#include "arena.h"
#include <memoryapi.h>
#include <sysinfoapi.h>




Arena *arena_create(size_t capacity) {

    LPVOID lpvResult = NULL;
    lpvResult = VirtualAlloc(
        NULL,
        capacity,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE 
    );

    if (lpvResult != NULL) {
        Arena *a = (Arena *) lpvResult;

        a->base = lpvResult;
        a->capacity = capacity;
        a->offset = sizeof(Arena);

        return a;
    }

    return NULL;
}


void *arena_alloc(Arena *a, size_t size, size_t align) {

    uintptr_t bump = (uintptr_t)a->base + a->offset;

    // aligned = (ptr + align - 1) & ~(align - 1);  -> guarantees power of 2 alignment
    bump = (bump + align - 1) & ~(align - 1);

    size_t new_offset = (bump - (uintptr_t)a->base) + size;
    if (new_offset > a->capacity) return NULL;

    a->offset = new_offset;
    return (void *)bump;
}

void arena_reset(Arena *a) {
    a->offset = sizeof(Arena);
}

void arena_destroy(Arena *a) {
    (void) VirtualFree(a->base, 0, MEM_RELEASE);
    a = NULL;
}