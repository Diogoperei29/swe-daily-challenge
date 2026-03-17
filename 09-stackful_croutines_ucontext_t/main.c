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

/*
typedef struct ucontext_t
  {
    unsigned long int __ctx(uc_flags);
    struct ucontext_t *uc_link;
    stack_t uc_stack;
    mcontext_t uc_mcontext;
    sigset_t uc_sigmask;
    struct _libc_fpstate __fpregs_mem;
    __extension__ unsigned long long int __ssp[4];
  } ucontext_t;
*/

/* Globals — a real library would hide these */
static Coroutine  coros[MAX_COROUTINES];
static int        coro_count  = 0;
static int        current     = -1;   /* index of currently running coroutine */

static char done_stack[4096];
static ucontext_t done_ctx;
static ucontext_t sched_ctx;          /* scheduler's own context */

void coro_yield(void){
    swapcontext(&coros[current].ctx, &sched_ctx);
}

void  coro_spawn(void (*fn)(void)) {
    getcontext(&coros[coro_count].ctx);
    coros[coro_count].id = coro_count;
    coros[coro_count].state = (CoroState)CORO_READY;
    coros[coro_count].stack = (char *)malloc(STACK_SIZE);
    coros[coro_count].ctx.uc_stack.ss_sp = coros[coro_count].stack;
    coros[coro_count].ctx.uc_stack.ss_size = STACK_SIZE;
    coros[coro_count].ctx.uc_link = &done_ctx;
    makecontext(&coros[coro_count].ctx, fn, 0);
    coro_count++;
}

static void finish_trampoline(void);   /* forward declaration */

void done_spawn(void (*fn)(void)) {
    getcontext(&done_ctx);
    done_ctx.uc_stack.ss_sp = done_stack;
    done_ctx.uc_stack.ss_size = sizeof(done_stack);
    done_ctx.uc_link = &sched_ctx;
    makecontext(&done_ctx, fn, 0);
}

void scheduler_run(void) {
    int completed_coro = 0;
    while (completed_coro < coro_count) {
        for(int cr = current + 1; cr < coro_count + current + 1; cr++) {
            if (coros[cr % coro_count].state == CORO_READY) {
                current = cr % coro_count;
                break;
            }
        }
        coros[current].state = CORO_RUNNING;
        swapcontext(&sched_ctx, &coros[current].ctx);
        if (coros[current].state == CORO_DONE) {
            completed_coro++;
            done_spawn(finish_trampoline); // done_ctx is consumed    
        } else {
            coros[current].state = CORO_READY;
        }
    }
}

void free_coros_stacks() {
    for(int cr = 0; cr < coro_count; cr++) {
        free(coros[cr].stack);
    }
}

/* ---- coroutines ---- */
static void worker(void) {
    int id = coros[current].id;
    for (int i = 0; i < 3; i++) {
        printf("coro %d: step %d\n", id, i);
        coro_yield();
    }
    printf("coro %d: done\n", id);
}

static void finish_trampoline(void) {
    coros[current].state = CORO_DONE;
}

int main(void) {

    done_spawn(finish_trampoline);

    coro_spawn(worker);
    coro_spawn(worker);
    coro_spawn(worker);
    coro_spawn(worker);
    scheduler_run();
    printf("all coroutines finished\n");
    free_coros_stacks();
    return 0;
}
