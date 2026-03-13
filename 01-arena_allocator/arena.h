// arena.h
#pragma once
#include <stddef.h>

typedef struct Arena {
    void *base;
    size_t capacity;
    size_t offset;
} Arena;

// Create a new arena backed by at least `capacity` bytes via VirtualAlloc.
// Returns NULL on failure.
Arena *arena_create(size_t capacity);

// Allocate `size` bytes aligned to `align` (must be a power of two).
// Returns NULL if the arena is exhausted.
void  *arena_alloc(Arena *a, size_t size, size_t align);

// Reset the arena: all memory is logically freed, but the mapping is kept.
void   arena_reset(Arena *a);

// Release the backing memory and invalidate the arena.
void   arena_destroy(Arena *a);