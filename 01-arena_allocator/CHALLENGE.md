
## Challenge [001] — Arena Allocator from Scratch

### Language
C (C11 or later). No C++ allocator machinery — raw `VirtualAlloc`/`VirtualFree` and pointer arithmetic only. Compile with MSVC (`cl`) or MinGW (`gcc`).

### Description
General-purpose `malloc`/`free` is often too slow, too unpredictable, or simply unavailable in constrained environments. Arena (a.k.a. linear/bump) allocators solve this by carving allocations sequentially out of a single large memory block and freeing everything at once with a single reset — zero per-object overhead, zero fragmentation, and cache-friendly layout.

Your task is to implement a fully functional arena allocator in C, backed by `VirtualAlloc` (not `malloc`). The arena must support arbitrary allocation sizes with correct alignment, a reset operation that reuses the memory without returning it to the OS, and an explicit destroy that releases the backing region. You may **not** call `malloc`, `calloc`, `realloc`, or `free` anywhere in your implementation.

The interface you must implement is given below — do not change the signatures:

```c
// arena.h
#pragma once
#include <stddef.h>

typedef struct Arena Arena;

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
```

Write a `main.c` that stress-tests your implementation: allocate structs of varying sizes and alignments, verify pointers are correctly aligned, fill allocations with a known byte pattern, verify no allocation overlaps a previous one, reset the arena, and repeat the cycle at least three times to confirm reset works correctly. Print a per-cycle summary to stdout.

### Research Guidance
- Search `VirtualAlloc` on Microsoft Learn — pay close attention to `MEM_RESERVE | MEM_COMMIT`, the `flProtect` flags (`PAGE_READWRITE`), and what alignment the returned pointer is guaranteed to have
- `VirtualFree` requires `MEM_RELEASE` and the original base address — understand why you cannot free a sub-region
- Read about **natural alignment** and **alignment padding**: how to round a pointer up to the next multiple of `align` using bitwise arithmetic — powers of two have a useful property: `(ptr + align - 1) & ~(align - 1)`
- Look up `_Alignof` and `offsetof` in C11 — you'll need them to place the `Arena` struct header **inside** the allocated region itself (no separate allocation allowed)
- Search for "bump pointer allocator" or "linear allocator" in game dev / embedded literature (e.g., *Game Programming Patterns* memory chapter or *Embedded Artistry*'s allocator series) — these give you the mental model without handing you the code

### Topics Covered
`VirtualAlloc`, `VirtualFree`, `MEM_COMMIT`, `MEM_RESERVE`, arena allocator, bump pointer, alignment padding, power-of-two alignment, memory layout, C11

### Completion Criteria
1. `arena_create` uses `VirtualAlloc` — no call to `malloc`/`calloc` anywhere in `arena.c` (verifiable with a text search).
2. The `Arena` struct header lives **inside** the allocated region (no separate heap allocation for the struct).
3. `arena_alloc` returns pointers satisfying `(uintptr_t)ptr % align == 0` for every alignment tested (at minimum: 1, 4, 8, 16, 64).
4. No two allocations returned by the same arena overlap, verified by the test loop in `main.c`.
5. After `arena_reset`, the arena correctly serves a fresh sequence of allocations without re-allocating (confirmed by checking the base pointer is unchanged across reset cycles).
6. `arena_destroy` is called at the end, and running the program under **Dr. Memory** (`drmemory -- arena_test.exe`) or the **MSVC CRT debug heap** reports no leaks.

### Difficulty Estimate
~30 min (knowing VirtualAlloc + alignment arithmetic) | ~60–90 min (researching from scratch)

### Category
Systems Programming

---