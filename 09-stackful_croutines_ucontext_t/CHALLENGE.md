## Challenge [009] — Stackful Coroutines via `ucontext_t`

### Language
C

### Description
Cooperative multitasking is the backbone of many lightweight runtime systems — from early operating systems to modern event loops and green-thread libraries. In this challenge you will implement a minimal coroutine scheduler in C using the POSIX `ucontext_t` API.

Your scheduler must support at least **four coroutines** running concurrently (in addition to a "main" context), each yielding control back to a central scheduler, which round-robins through ready coroutines. When a coroutine finishes, the scheduler removes it from the run queue and continues with the remaining ones.

Start from the scaffold in `main.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define MAX_COROUTINES  8
#define STACK_SIZE      (64 * 1024)   /* 64 KiB per coroutine */

typedef enum { CORO_READY, CORO_RUNNING, CORO_DONE } CoroState;

typedef struct {
    ucontext_t  ctx;
    char       *stack;
    CoroState   state;
    int         id;
} Coroutine;

/* Globals — a real library would hide these */
static Coroutine  coros[MAX_COROUTINES];
static int        coro_count  = 0;
static int        current     = -1;   /* index of currently running coroutine */
static ucontext_t sched_ctx;          /* scheduler's own context */

/* TODO: implement these */
void coro_yield(void);
int  coro_spawn(void (*fn)(void));
void scheduler_run(void);

/* ---- example coroutines ---- */
static void worker(void) {
    int id = coros[current].id;
    for (int i = 0; i < 3; i++) {
        printf("coro %d: step %d\n", id, i);
        coro_yield();
    }
    printf("coro %d: done\n", id);
}

int main(void) {
    coro_spawn(worker);
    coro_spawn(worker);
    coro_spawn(worker);
    coro_spawn(worker);
    scheduler_run();
    printf("all coroutines finished\n");
    return 0;
}
```

Implement `coro_spawn`, `coro_yield`, and `scheduler_run` so that the output interleaves steps from all four coroutines in round-robin order, then prints `all coroutines finished`.

---

### Background & Teaching

#### Core Concepts

**What is a coroutine?**

A coroutine is a function that can suspend its own execution mid-way through and later resume from exactly where it left off — with all its local variables intact. **Unlike a preemptive thread, a coroutine never suspends involuntarily**; it must explicitly yield control. This cooperative model eliminates data races within a single-threaded scheduler and removes the need for most locks.

**Stackful vs. stackless coroutines**

There are two families:

- **Stackless** (C++20 `co_await`, Python `async/await`, Rust's `async fn`): the compiler transforms the function into a state machine. There is no separate stack; locals that must survive a suspension point are moved into a heap-allocated frame. Extremely memory-efficient but requires compiler support and can only suspend at well-defined `await` points.

- **Stackful** (Go goroutines, Lua coroutines, `ucontext_t`, `setjmp`/`longjmp`-based): each coroutine has its own full call stack. You can call nested functions and yield from *anywhere* in the call tree. The entire stack is preserved on suspension. This is what you are implementing today.

**The CPU execution context**

To switch from coroutine A to coroutine B, you need to save and restore:

1. **General-purpose registers** — rsp, rbp, rbx, r12–r15 on x86-64 (callee-saved per the System V ABI). The caller-saved ones (rdi, rsi, rdx, …) are by definition not live across a function call boundary.
2. **The stack pointer** — this is what makes a coroutine "stackful". Each coroutine has its own stack region. Switching `rsp` switches the entire call stack.
3. **The instruction pointer** — where to resume (stored implicitly as the return address at the top of the stack after a `call`).
4. **Signal mask** (optionally) — `ucontext_t` always saves and restores the signal mask (via `sigprocmask`), which is a syscall. This is the main overhead of `ucontext_t` vs. raw assembly context switch (`getcontext`/`setcontext` are ~3× more expensive than a hand-rolled swap for this reason).

**`ucontext_t` API**

POSIX defines four functions:

```c
int  getcontext(ucontext_t *ucp);           // save current context into ucp
int  setcontext(const ucontext_t *ucp);     // restore context from ucp — does NOT return
void makecontext(ucontext_t *ucp,           // set up a new context to run fn()
                 void (*fn)(void), int argc, ...);
int  swapcontext(ucontext_t *oucp,          // save current into oucp, restore from ucp
                 const ucontext_t *ucp);
```

The typical sequence to create a new coroutine:

```c
getcontext(&co->ctx);               // 1. populate uc_mcontext with sane defaults
co->ctx.uc_stack.ss_sp   = stack;   // 2. point the coroutine at its stack buffer
co->ctx.uc_stack.ss_size = STACK_SIZE;
co->ctx.uc_link          = &sched_ctx; // 3. when fn() returns, jump here
makecontext(&co->ctx, fn, 0);       // 4. modify ctx so that setcontext would call fn()
```

`uc_link` is crucial: it is the context that `setcontext` will automatically resume when the coroutine function **returns** naturally (i.e., falls off the end of `fn`). If you set `uc_link = &sched_ctx`, returning from the coroutine function automatically hands control back to the scheduler without any explicit yield call.

**How `makecontext` works internally**

`makecontext` modifies the stack buffer and the saved `rip`/`rsp` inside `ucp->uc_mcontext` so that when `setcontext(ucp)` is called, execution begins at `fn`. It does this by placing a return address (pointing to a libc trampoline that calls `setcontext(uc_link)`) at the top of the coroutine's stack, then setting `rsp` to point just below it. When `fn` executes a `ret`, it jumps to that trampoline.

**The round-robin scheduler**

The simplest scheduler keeps a list of coroutines in CORO_READY or CORO_RUNNING state. On each scheduling decision it picks the next READY coroutine (cycling with modulo arithmetic), sets its state to RUNNING, updates `current`, and calls `swapcontext(&sched_ctx, &coro->ctx)`. The scheduler itself runs in the `sched_ctx` context; it resumes here each time a coroutine calls `coro_yield()` or finishes.

`coro_yield()` is just:
```c
void coro_yield(void) {
    swapcontext(&coros[current].ctx, &sched_ctx);
}
```
This saves the coroutine's current registers+stack into `coros[current].ctx` and jumps back to the scheduler.

#### How to Approach This in C

1. **Allocate stacks with `malloc`** (or `mmap` + `mprotect` for guard pages, but `malloc` is fine here). Assign `uc_stack.ss_sp` and `uc_stack.ss_size`. Do **not** use a VLA or a stack-local array as the coroutine's stack — when your function returns, that memory is gone.

2. **Always call `getcontext` before `makecontext`**. `makecontext` only *modifies* a context; it does not fully initialise one. Calling `makecontext` on an uninitialised `ucontext_t` is undefined behaviour — the signal mask and floating-point state fields will be garbage.

3. **Set `uc_link` before `makecontext`**. The trampoline that `makecontext` installs reads `uc_link` at *call time* — it is baked into the ABI contract, but the safest habit is to always set it before the `makecontext` call.

4. **Track state explicitly**. When `scheduler_run` resumes after a `swapcontext` returns, it needs to know why: did the coroutine yield (CORO_READY) or finish (CORO_DONE via `uc_link`)? The easiest approach: inside `scheduler_run`, after `swapcontext` returns, check whether `coros[current].state == CORO_RUNNING`. If it is still RUNNING, the coroutine yielded (set it back to READY). If `uc_link` fired, the coroutine's function has returned — at that point you need a way to mark it DONE. The trick: make the `uc_link` target a context that marks the current coroutine DONE and then falls into the scheduler loop again. Or simply: inside your scheduler loop, after `swapcontext` returns, mark the coroutine READY, and have the coroutine set its own state to DONE right before returning from its function — but the cleanest approach is to wrap every coroutine entry point in a trampoline that sets CORO_DONE after `fn()` returns.

5. **Free stacks** when coroutines finish. Every coroutine that completes leaves a malloc'd stack that must be freed. Don't leak it.

#### Key Pitfalls & Security/Correctness Gotchas

- **`getcontext` before `makecontext` is mandatory.** If you skip it, `makecontext` operates on uninitialised memory. This may crash immediately or produce bizarre control flow that only manifests under certain conditions. Always call `getcontext` first.

- **Stack growth direction.** On x86-64 Linux, stacks grow downward. `uc_stack.ss_sp` must point to the **bottom** of the buffer (lowest address), and `uc_stack.ss_size` is the total size. `makecontext` internally sets `rsp` to `ss_sp + ss_size - alignment`. Do NOT pass `ss_sp = stack + STACK_SIZE` thinking you're being clever — you'll just point the stack into invalid memory.

- **`uc_link = NULL` causes `_exit`**. If `uc_link` is NULL and the coroutine function returns, the process exits. This is not a segfault — it silently kills your whole program. Always set `uc_link`.

- **The signal mask overhead.** `swapcontext` calls `sigprocmask` under the hood. For a scheduler switching thousands of times per second this is measurable. In production code, people use `_swapcontext` assembly stubs or `setjmp`/`longjmp` + a separate stack pointer swap to avoid the syscall.

- **Do not use floating-point in coroutines without considering FPU state.** `ucontext_t` does save the x87/SSE FPU state (via `FXSAVE`) on glibc x86-64 — but this is architecture-specific. On some older or non-glibc environments it does not. If portability matters, verify.

- **Stack size.** 64 KiB is sufficient for this challenge. Real green-thread runtimes start at 2–8 KiB and grow the stack on demand using guard page faults (Go's "stack copying"). Overflowing a 64 KiB stack silently corrupts memory — there is no guard page in this scaffold.

#### Bonus Curiosity (Optional — if you finish early)

- **Boost.Context / libco**: These are production-quality context-switch libraries that skip the signal mask syscall by using hand-rolled assembly. Reading the x86-64 assembly stub in Boost.Context (roughly 8 instructions) clarifies exactly what a context switch costs.

- **Go's goroutine scheduler (G-M-P model)**: Go's runtime implements stackful coroutines with growable stacks, work-stealing across OS threads, and a cooperative+preemptive hybrid (since Go 1.14, goroutines can be preempted at any safe point via signal injection). A fascinating evolution of the exact model you're building today.

- **`sigaltstack`**: A separate signal stack for your process. Linux signal delivery can use the coroutine's stack if a signal fires during its execution — potentially overflowing a small coroutine stack. `sigaltstack` sidesteps this. Worth reading the man page.

---

### Research Guidance

- `man 3 makecontext` — read the NOTES section carefully; it explains the `getcontext` requirement and `uc_link` semantics.
- `man 3 swapcontext` — understand exactly what is saved and restored.
- `man 3 getcontext` — pay attention to the portability note about `uc_link` and the warning about `volatile` locals.
- The glibc source for `sysdeps/unix/sysv/linux/x86_64/swapcontext.S` — seeing the ~20 lines of assembly demystifies the whole API.
- "Fibers, Oh My!" by Eli Bendersky — benchmarks `ucontext_t` vs. hand-rolled context switches.

### Topics Covered
`ucontext_t`, stackful coroutines, cooperative scheduling, context switching, round-robin scheduler, `makecontext`, `swapcontext`, `uc_link`, stack allocation, coroutine lifecycle

### Completion Criteria
1. `09/main.c` compiles with `gcc -Wall -Wextra -o coro main.c` and zero warnings.
2. Running `./coro` prints steps from all four coroutines interleaved in round-robin order (each coroutine prints its three steps before any prints a fourth).
3. Each coroutine's "done" line appears exactly once, and `all coroutines finished` is printed last.
4. No memory is leaked: running under `valgrind --leak-check=full ./coro` reports "0 bytes in 0 blocks" for definitely lost allocations.
5. `coro_yield()` is implemented with a single `swapcontext` call back to the scheduler context — no `sleep`, `setjmp`, or busy-wait.
6. Coroutine stacks are heap-allocated (not stack-local arrays or globals).

### Difficulty Estimate
~15 min (knowing the domain) | ~60 min (researching from scratch)

### Category
Concurrency & Parallelism
