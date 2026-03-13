## Challenge [006] — Minimal epoll Reactor in C

### Language
C (C11)

### Description
You're building a lightweight event loop for a daemon that needs to handle
multiple concurrent TCP connections without threads. Modern Linux provides
`epoll` — a scalable I/O event notification interface that doesn't degrade
with the number of watched file descriptors the way `select()` and `poll()` do.

Your task: implement a **minimal reactor** in C using `epoll` that can:

1. Accept new TCP connections on a listening socket
2. Read data from connected clients and echo it back
3. Handle client disconnections cleanly
4. All without blocking or threads — single-threaded, event-driven

Here's the starter scaffold:

```c
// reactor.c — fill in the TODOs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#define PORT        8080
#define MAX_EVENTS  64
#define BUF_SIZE    4096

static int make_nonblocking(int fd) {
    // TODO: use fcntl to set O_NONBLOCK on fd
    // Return -1 on failure, 0 on success
}

static int create_listen_socket(int port) {
    // TODO: create a TCP socket, set SO_REUSEADDR, bind to port, listen
    // Make it non-blocking before returning
    // Return the fd, or -1 on error
}

static void handle_new_connection(int epfd, int listenfd) {
    // TODO: accept all pending connections in a loop (because edge-triggered)
    // For each: make non-blocking, register with epoll for EPOLLIN|EPOLLET|EPOLLRDHUP
}

static void handle_client_data(int epfd, int clientfd) {
    // TODO: read all available data in a loop (because edge-triggered)
    // Echo it back. On EAGAIN/EWOULDBLOCK: stop. On EOF or error: close + deregister
}

int main(void) {
    int listenfd = create_listen_socket(PORT);
    if (listenfd < 0) { perror("listen"); return 1; }

    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return 1; }

    // TODO: register listenfd with epoll (EPOLLIN | EPOLLET)

    struct epoll_event events[MAX_EVENTS];
    printf("Reactor listening on port %d\n", PORT);

    for (;;) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            // TODO: dispatch to handle_new_connection or handle_client_data
            //       also handle EPOLLHUP and EPOLLERR
        }
    }

    return 0;
}
```

Compile with: `gcc -Wall -Wextra -O2 -o reactor reactor.c`

Test with `nc localhost 8080` — type text, verify it echoes back. Open two
terminal windows concurrently to confirm both are served without one blocking
the other.

---

### Background & Teaching

This section is the core of the challenge. Read it before writing a single line.

#### Core Concepts

**The Problem with `select()` and `poll()`**

The oldest way to do non-blocking I/O multiplexing on Linux is `select()`. It
works, but it has a fatal scalability flaw: every call to `select()` forces the
kernel to scan every single watched file descriptor to find which ones are ready.
With 10,000 connections, that's 10,000 FDs scanned on every wakeup — even if
only one is ready. The time complexity is O(N) per `select()` call, and there's
a hard limit of `FD_SETSIZE` (typically 1024) on the number of watched FDs.
`poll()` removes that hard limit but retains the O(N) scan on every call.

**What epoll Does Differently**

`epoll` inverts the model. Instead of scanning every fd on every call, you
*register* interest in fds once via `epoll_ctl`. The kernel maintains an
internal data structure (a red-black tree keyed by fd). When an fd becomes
ready, the kernel places it on a ready list. `epoll_wait` returns only the fds
that are *actually ready* — it never scans the full watched set. The result is
O(1) per ready event regardless of how many fds are registered.

**Level-Triggered vs Edge-Triggered — The Most Critical Distinction**

`epoll` supports two triggering modes, and understanding the difference is
mandatory before you write a single line:

- **Level-triggered (LT)** — the default. `epoll_wait` keeps returning an fd as
  "ready" as long as there is unread data sitting in its kernel receive buffer.
  If you read 100 bytes of a 500-byte message and then call `epoll_wait` again,
  the fd fires immediately again. This is safe and forgiving — partial reads
  work fine.

- **Edge-triggered (ET)** — activated with `EPOLLET`. `epoll_wait` notifies you
  exactly **once** when the fd *transitions* from not-ready to ready — i.e.,
  when new data arrives or a new connection appears. If you don't drain the
  entire buffer in that handler invocation, the remaining data sits in the
  buffer silently — you will receive **no further notification** until new data
  arrives again. For a TCP echo server this means if 5 KB arrives and you only
  read 1 KB, the other 4 KB is stuck forever from the reactor's perspective.

The correct ET read pattern is:

```c
while (1) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // buffer drained
        // real error — close fd and deregister
        break;
    }
    if (n == 0) {
        // EOF — peer closed their write end
        break;
    }
    // process (echo) the data...
}
```

**Non-blocking I/O is mandatory with ET.** If the fd is blocking and you call
`read()` after draining the buffer, the call blocks indefinitely — your entire
event loop stalls. Setting `O_NONBLOCK` ensures `read()` returns `EAGAIN`
immediately when there is nothing left to read.

**The Reactor Pattern**

What you are building is the *reactor* design pattern: a central event loop
waits for I/O readiness events, dispatches them synchronously to handlers, and
returns to waiting. The loop never blocks except inside `epoll_wait`. All
handlers run to completion. There are no threads and no callbacks scheduled for
later — just a tight dispatch loop. This is the core architecture of nginx,
Redis, Node.js (via libuv), and HAProxy.

**The Three epoll Primitives**

| Call | Purpose |
|------|---------|
| `epoll_create1(0)` | Create the epoll instance; returns an epoll fd |
| `epoll_ctl(epfd, op, fd, &event)` | Add, modify, or delete an fd from the watch set |
| `epoll_wait(epfd, events, maxevents, timeout)` | Block until events arrive; writes them into `events[]` |

`op` for `epoll_ctl` is one of:
- `EPOLL_CTL_ADD` — register a new fd
- `EPOLL_CTL_MOD` — change the event mask of a registered fd
- `EPOLL_CTL_DEL` — remove an fd (pass `NULL` for `event` on Linux ≥ 2.6.9)

**The `epoll_event` Structure**

```c
struct epoll_event {
    uint32_t     events;  // bitmask
    epoll_data_t data;    // union: .fd (int), .ptr (void*), .u32, .u64
};
```

The `data` field is opaque storage — the kernel stores it verbatim and returns
it to you in the ready event. Use `data.fd` to carry the file descriptor number
so you know which connection fired when `epoll_wait` returns.

**Important event flags:**
- `EPOLLIN` — data available to read
- `EPOLLOUT` — socket ready to write (buffer not full)
- `EPOLLET` — edge-triggered mode
- `EPOLLRDHUP` — peer closed the write end (saves a `read()` to detect EOF)
- `EPOLLHUP` — hangup (always reported even if not requested)
- `EPOLLERR` — error condition (always reported even if not requested)

**Accepting connections with ET**

The listening socket must also be non-blocking and registered with `EPOLLET`.
When it fires, multiple clients may have connected between the last `epoll_wait`
call and now — but ET only gives you one notification. You must loop `accept()`
until it returns `EAGAIN`, otherwise pending connections sit in the kernel's
accept queue and starve.

#### How to Approach This in C

1. **`make_nonblocking(fd)`**: Use `fcntl(fd, F_GETFL, 0)` to fetch the current
   flags, then `fcntl(fd, F_SETFL, flags | O_NONBLOCK)`. Always fetch first —
   do not hardcode flags, as other bits (e.g., `O_RDWR`) must be preserved.

2. **`create_listen_socket(port)`**:
   - `socket(AF_INET, SOCK_STREAM, 0)` to create a TCP socket
   - `setsockopt(..., SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))` so the
     port is reusable immediately after the server exits
   - `bind()` the socket to `INADDR_ANY` on the given port
   - `listen(fd, SOMAXCONN)` to start accepting
   - Call `make_nonblocking()` before returning

3. **Registering with epoll**:
   ```c
   struct epoll_event ev = {0};
   ev.events  = EPOLLIN | EPOLLET;
   ev.data.fd = fd;
   if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) { perror("epoll_ctl"); }
   ```

4. **Dispatching**: In the event loop, check `events[i].data.fd == listenfd`
   to route to `handle_new_connection`; otherwise route to `handle_client_data`.
   Before that check, always test `events[i].events & (EPOLLHUP | EPOLLERR)`
   and treat those as a close event.

5. **`handle_new_connection`**: Call `accept4(listenfd, NULL, NULL,
   SOCK_NONBLOCK)` in a loop (the `SOCK_NONBLOCK` flag sets O_NONBLOCK in one
   syscall). Loop until `accept4` returns -1 with `errno == EAGAIN`. Register
   each accepted fd with `EPOLLIN | EPOLLET | EPOLLRDHUP`.

6. **`handle_client_data`**: Inner read loop until `EAGAIN`. Echo back with
   `write()`. On `n == 0` (EOF) or `EPOLLRDHUP`: call
   `epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)`, then `close(fd)`.

#### Key Pitfalls & Security/Correctness Gotchas

- **Forgetting to drain on ET**: With `EPOLLET`, a single `read()` call is
  never sufficient. If data remains in the receive buffer after you return from
  the handler, no further notification arrives until new data comes in. Always
  loop until `EAGAIN`.

- **Accepting only one connection per event**: The listen socket fires once per
  "batch" of incoming connections with ET. If 5 clients connect simultaneously,
  you must call `accept()` 5 times. Accepting once leaves 4 connections silently
  stuck in the kernel's accept queue.

- **Not setting O_NONBLOCK on accepted fds**: Accepted sockets do not
  automatically inherit `O_NONBLOCK` from the listening socket. Set it
  explicitly on every accepted fd before registering with epoll.

- **Uninitialized `epoll_event`**: `struct epoll_event` is not zero-initialized
  by default in C. Always set `ev.data.fd` (or use `= {0}` initialization)
  before calling `epoll_ctl`. Reading back garbage from `events[i].data.fd`
  is undefined behavior.

- **Skipping `EPOLL_CTL_DEL` before `close()`**: On modern kernels, `close()`
  removes the fd from epoll automatically — but only if the fd's refcount drops
  to zero. If the fd was duplicated (e.g., via `dup()` or across a `fork()`),
  `close()` won't remove it and stale events keep firing. Calling
  `EPOLL_CTL_DEL` explicitly is always safe.

- **Ignoring `EPOLLHUP` and `EPOLLERR`**: These flags are delivered by the
  kernel even if you never put them in your event mask. If your dispatch loop
  only checks `EPOLLIN` and falls through, a hung-up fd causes an infinite
  busy-loop: `epoll_wait` keeps returning immediately with these flags set, and
  you keep ignoring them, pinning a CPU core at 100%.

#### Bonus Curiosity (Optional — if you finish early)

- **`io_uring` vs epoll**: `io_uring` (Linux 5.1+) takes the next logical step
  — instead of notifying you that a read is *possible*, it performs the read
  asynchronously and notifies you when it *completes*. Communication happens
  via two lock-free ring buffers shared between userspace and kernel (submission
  queue and completion queue), with zero syscalls for the fast path. Redis 7.x
  added an `io_uring` backend. Compare the mental model to the reactor you just
  built: `man io_uring` + `man liburing`.

- **The Thundering Herd and `EPOLLEXCLUSIVE`**: If you fork multiple worker
  processes that all call `epoll_wait` on the same listening socket, every
  incoming connection wakes all workers — but only one can `accept()` it. The
  rest return immediately, having done wasted work. This is the "thundering
  herd". Linux 4.5 added `EPOLLEXCLUSIVE` to route each event to exactly one
  waiting process. Worth understanding for any multi-process server (nginx's
  `accept_mutex` predates this and solved it in userspace).

- **libuv's abstraction over epoll**: Node.js's event loop runs on libuv, which
  wraps epoll (Linux), kqueue (macOS/BSD), and IOCP (Windows) behind a single
  unified API. The way libuv distinguishes *handles* (persistent I/O objects
  like TCP sockets) from *requests* (one-shot operations like a write) maps
  directly to the reactor you just built. The source is in `deps/uv/src/unix/tcp.c`
  in the Node.js repo — reading it after building your own reactor is a clean
  "aha" moment.

---

### Research Guidance

- `man 7 epoll` — read the entire manual; pay special attention to the
  "Edge-triggered and level-triggered" section and the notes on `close()`
  and file descriptor lifetime semantics
- `man 2 epoll_ctl` — understand `EPOLL_CTL_ADD/MOD/DEL`, the full event
  bitmask (`EPOLLRDHUP`, `EPOLLONESHOT`, `EPOLLEXCLUSIVE`, `EPOLLWAKEUP`)
- `man 2 accept4` — the `SOCK_NONBLOCK` flag; understand why it saves a
  separate `fcntl()` call and why that matters at high connection rates
- `man 2 fcntl` — specifically `F_GETFL` / `F_SETFL` / `O_NONBLOCK`; understand
  why you must fetch-then-OR instead of just writing the new flags
- Dan Kegel, "The C10K Problem" (kegel.com/c10k.html) — the historical
  motivation that drove epoll's design; still one of the clearest explanations
  of why poll/select don't scale

### Topics Covered
epoll, edge-triggered I/O, level-triggered I/O, reactor pattern, non-blocking sockets, O_NONBLOCK, fcntl, EAGAIN/EWOULDBLOCK, SO_REUSEADDR, EPOLLRDHUP, EPOLLHUP, EPOLLERR, accept4, event-driven I/O, single-threaded concurrency, I/O multiplexing

### Completion Criteria
1. The program compiles with `gcc -Wall -Wextra -O2` and zero warnings.
2. Running `./reactor` accepts TCP connections on port 8080; text sent via `nc localhost 8080` is echoed back verbatim.
3. Two `nc` clients connected simultaneously both receive their independent echoes without one blocking the other.
4. The implementation uses `EPOLLET` (edge-triggered) mode and correctly drains all data in a read loop until `EAGAIN`/`EWOULDBLOCK`.
5. Disconnecting a client (Ctrl-C on `nc`) does not crash the server; the fd is properly closed and removed from epoll.
6. `EPOLLHUP` and `EPOLLERR` events are checked and handled to prevent infinite busy-looping on error conditions.

### Difficulty Estimate
~20 min (knowing the domain) | ~60 min (researching from scratch)

### Category
Concurrency & Parallelism
