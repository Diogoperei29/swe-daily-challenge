
## Challenge [002] — Treiber Lock-Free Stack

### Language
C++ (C++17 or later). Compile with GCC or Clang: `-std=c++17 -Wall -Wextra -pthread`.

### Description
Mutexes are the safe, boring path to thread safety. Lock-free data structures are the path that actually forces you to understand the C++ memory model. Your task is to implement the **Treiber stack** — a classic singly-linked lock-free stack where `push` and `pop` both spin on a compare-and-exchange (CAS) loop against an atomic head pointer. No `std::mutex`, no `std::spinlock`, no OS synchronization primitives whatsoever.

The implementation must be generic (templated on the value type). At every atomic operation you must choose a `memory_order` tighter than the default `seq_cst` and justify it in a one-line comment. Using `seq_cst` everywhere is not acceptable — understand what you actually need and why, then document it.

Once the stack compiles, write a stress test in `main.cpp` that spawns **4 producer threads** each pushing 10 000 integers (producer *i* pushes values in the range `[i*10000, (i+1)*10000)`), and **4 consumer threads** that pop until a shared `done` flag is set and the stack is empty. After all threads join, verify that every pushed value was popped exactly once — no losses, no duplicates. Use a `std::unordered_set` or a `std::vector` + sort-then-compare strategy.

There is a well-known hazard lurking here: the **ABA problem**. Your implementation does not need to solve it (solutions require hazard pointers, tagged pointers, or epoch-based reclamation — all beyond this challenge's scope). However, at the bottom of `main.cpp`, add a block comment titled `// ABA ANALYSIS` that describes: (a) the exact three-thread interleaving that triggers ABA on your stack, (b) which line/operation is the victim, and (c) why your stress test is unlikely to trigger it in practice.

```cpp
// Starter stub — fill in the bodies:
#pragma once
#include <atomic>
#include <optional>
#include <cstddef>

template <typename T>
class TreiberStack {
    struct Node {
        T       value;
        Node*   next;
        explicit Node(T v) : value(std::move(v)), next(nullptr) {}
    };

    std::atomic<Node*> head_{ nullptr };

public:
    // Push val onto the stack. Never fails.
    void push(T val);

    // Pop the top element. Returns std::nullopt if empty.
    std::optional<T> pop();

    // True iff the stack contains no elements (may race — use only for hints).
    bool empty() const noexcept;

    // Must not leak. Assumes no concurrent access during destruction.
    ~TreiberStack();
};
```

### Research Guidance
- `std::compare_exchange_weak` vs `std::compare_exchange_strong` — what does "spurious failure" mean, and why is the weak variant preferred in a retry loop despite it?
- `std::memory_order_acquire`, `memory_order_release`, `memory_order_acq_rel` — draw the happens-before arrows for a successful CAS in `push` followed by a successful CAS in `pop`; which orderings make the stored `next` pointer visible to the popper?
- `std::atomic<T*>::is_lock_free()` — is a pointer-width atomic guaranteed to be lock-free on x86-64? What about 32-bit ARM?
- The ABA problem — search "Treiber stack ABA problem" and read at least one write-up that shows the exact interleaving
- `std::atomic_thread_fence` — understand how it differs from per-operation ordering; you probably won't need it, but knowing it exists clarifies why per-operation ordering is usually sufficient

### Topics Covered
C++ atomics, lock-free programming, Treiber stack, compare-and-exchange (CAS), `memory_order`, happens-before, ABA problem, C++ memory model

### Completion Criteria
1. The project compiles with `-std=c++17 -Wall -Wextra -pthread` and **zero warnings**.
2. No `std::mutex`, `std::lock_guard`, `sem_t`, or any OS synchronization primitive appears anywhere in `treiber_stack.h`.
3. Every `std::atomic` operation carries an explicit `memory_order` argument (no implicit `seq_cst` defaults), and each is accompanied by a one-line comment explaining the choice.
4. The 4×4 stress test (4 producers × 10 000 pushes, 4 consumers) terminates and reports that all 40 000 values were received exactly once — no missing values, no duplicates.
5. Running under **ThreadSanitizer** (`-fsanitize=thread`) reports no data races.
6. The `// ABA ANALYSIS` comment block correctly identifies the three-step interleaving, the vulnerable operation, and explains why the stress test is unlikely to hit it.

### Difficulty Estimate
~20 min (knowing the C++ memory model) | ~60–75 min (researching from scratch)

### Category
Concurrency & Parallelism
