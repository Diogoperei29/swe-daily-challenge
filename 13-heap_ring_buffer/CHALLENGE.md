## Challenge [13] — Unsafe Rust: Heap-Allocated Ring Buffer with Raw Pointers

### Language
Rust (stable toolchain). No external crates. `unsafe` blocks are required and must each carry a precise `// SAFETY:` comment.

### Description

A **ring buffer** (also called a circular buffer) is a fixed-capacity FIFO backed by a single contiguous heap allocation. Two cursors — `head` (write index) and `tail` (read index) — advance independently and wrap modulo capacity, giving O(1) enqueue and dequeue with zero per-element allocation overhead. It is the data structure inside almost every kernel I/O ring, audio pipeline, and lock-free SPSC queue ever built.

Your task is to implement `RingBuffer<T>` — a generic, heap-allocated ring buffer — **without** using `Vec`, `VecDeque`, or any container from `std`. You must:

1. Allocate raw memory with `std::alloc::alloc` using a `Layout`.
2. Store elements in `MaybeUninit<T>` slots so the type system does not assume they are initialised.
3. Read and write elements through raw pointer operations (`ptr::write`, `ptr::read`).
4. Implement `Drop` to destroy every live element, then deallocate the backing array.
5. Provide `unsafe impl Send` / `unsafe impl Sync` with a written soundness argument for each.

Every `unsafe` block must be preceded by a `// SAFETY:` comment that names the exact invariant being upheld. This is not optional bureaucracy — it is what makes unsafe Rust auditable.

**Scaffold** — fill in each `todo!()`:

```rust
// src/lib.rs
use std::alloc::{alloc, dealloc, Layout};
use std::mem::MaybeUninit;
use std::ptr::NonNull;

pub struct RingBuffer<T> {
    buf:  NonNull<MaybeUninit<T>>,
    cap:  usize,
    head: usize,  // index of the next slot to write into  (always < cap)
    tail: usize,  // index of the next slot to read from   (always < cap)
    len:  usize,
}

impl<T> RingBuffer<T> {
    /// Creates a ring buffer with exactly `capacity` slots.
    /// # Panics
    /// Panics if `capacity == 0` or if the allocator returns null.
    pub fn new(capacity: usize) -> Self { todo!() }

    /// Enqueues `val`. Returns `Err(val)` if the buffer is full.
    pub fn push(&mut self, val: T) -> Result<(), T> { todo!() }

    /// Removes and returns the oldest element, or `None` if empty.
    pub fn pop(&mut self) -> Option<T> { todo!() }

    pub fn len(&self)      -> usize { self.len }
    pub fn is_empty(&self) -> bool  { self.len == 0 }
    pub fn is_full(&self)  -> bool  { self.len == self.cap }
}

impl<T> Drop for RingBuffer<T> {
    fn drop(&mut self) { todo!() }
}
```

```rust
// src/main.rs
use ring_buffer::RingBuffer;

fn main() {
    let mut rb: RingBuffer<String> = RingBuffer::new(4);

    rb.push("alpha".into()).unwrap();
    rb.push("beta".into()).unwrap();
    rb.push("gamma".into()).unwrap();

    println!("{:?}", rb.pop()); // Some("alpha")
    println!("{:?}", rb.pop()); // Some("beta")

    rb.push("delta".into()).unwrap();
    rb.push("epsilon".into()).unwrap();
    assert!(rb.push("zeta".into()).is_err()); // full → Err

    while let Some(s) = rb.pop() {
        println!("{s}"); // gamma, delta, epsilon
    }
    println!("{:?}", rb.pop()); // None
}
```

---

### Background & Teaching

#### Core Concepts

**What is a ring buffer?**

A ring buffer allocates a fixed array of `N` slots. Two independent integer cursors track the write end (`head`) and the read end (`tail`). To enqueue: write into `buf[head % N]` and increment `head`. To dequeue: read from `buf[tail % N]` and increment `tail`. Neither cursor ever decrements — both grow monotonically — but their difference modulo `N` gives the current occupancy. The key insight: when `head` reaches `N` you wrap it back to 0, so you are perpetually reusing the same fixed slab. This is the data structure inside Linux's `io_uring` submission/completion rings, UART ISR buffers, audio DMA rings, and virtually every lock-free SPSC queue in production code.

**`MaybeUninit<T>` — why it exists**

In Rust, every value of type `T` carries a **validity invariant**: its bit pattern must represent a legal `T`. If `T = String`, a `String` is a (`ptr`, `len`, `cap`) triple; the pointer must be non-null and point to valid UTF-8. Allocating a `[String; N]` slab and leaving the slots "empty" is immediately undefined behaviour — the compiler is free to assume any `String` it touches is valid and will generate code that dereferences those garbage pointers.

`MaybeUninit<T>` sidesteps this by wrapping `T` in a union:

```rust
union MaybeUninit<T> {
    uninit: (),
    value:  ManuallyDrop<T>,
}
```

It has **no validity invariant** (any bit pattern is acceptable), **no destructor** (the union body is `ManuallyDrop`), and **no niche** (the compiler cannot assume e.g. that the inner pointer is non-null). You promise Rust: "I know whether this slot holds a live `T`; I will track that myself." Only once you have written a legitimate `T` into the slot are you allowed to call `.assume_init()` or `ptr::read()` on it.

**`NonNull<T>` — a non-null raw pointer**

`*mut T` can be null, and Rust's pointer model says: a null pointer may never be dereferenced, even in unsafe code. `NonNull<T>` is a guaranteed-non-null thin pointer with two important properties:

- **Niche optimisation**: `Option<NonNull<T>>` is the same size as `NonNull<T>` (null encodes `None`), because the compiler knows a valid `NonNull` is never null.
- **Explicit intent**: wrapping your allocation in `NonNull` immediately documents "this is a heap allocation we own; null is a bug."

You create it with `NonNull::new(raw_ptr)` (returns `Option`) or `NonNull::new_unchecked(raw_ptr)` (unsafe, panics only if you are wrong).

**Rust's aliasing rules (stacked borrows)**

Rust's memory model, formalised as *stacked borrows*, says: at any instant, a memory location is either mutably accessed by exactly one entity **or** shared-immutably accessed by many — never both simultaneously. The borrow checker enforces this for safe references. Raw pointers bypass the checker, but the rule still applies when you *create references from them*. Every `unsafe` block that dereferences a raw pointer must explicitly argue that no conflicting reference exists at that moment.

**Safety invariants and `// SAFETY:` comments**

A *safety invariant* is a property your data structure maintains that makes the `unsafe` operations inside it correct. Examples for your ring buffer:

- *"Every index `i` in `[tail, tail + len)` modulo `cap` holds a fully initialised `T`."*
- *"`buf` is non-null and points to a live heap allocation of exactly `cap * size_of::<MaybeUninit<T>>()` bytes with the alignment of `MaybeUninit<T>`."*
- *"`head` and `tail` are always strictly less than `cap`."*

A `// SAFETY:` comment names which invariant makes the next `unsafe` expression sound. Without it, the only thing an auditor can say about your code is "trust me."

**`Send` and `Sync` for raw-pointer types**

Raw pointers are `!Send` and `!Sync` by default because the compiler cannot verify thread safety on its own. You must opt in manually:

```rust
unsafe impl<T: Send> Send for RingBuffer<T> {}
unsafe impl<T: Sync> Sync for RingBuffer<T> {}
```

The soundness argument for `Send`: `RingBuffer<T>` *owns* its allocation exclusively; there is no shared state. Sending it to another thread transfers that exclusive ownership, which is safe as long as `T: Send`. The soundness argument for `Sync`: a shared `&RingBuffer<T>` exposes only read-only methods (`len`, `is_empty`, `is_full`), all of which observe plain `usize` fields with no interior mutability. Sharing immutable access is safe when `T: Sync`.

---

#### How to Approach This in Rust

**Allocation:**

```rust
assert!(capacity > 0, "capacity must be > 0");
let layout = Layout::array::<MaybeUninit<T>>(capacity)
    .expect("layout overflow");
let raw = unsafe { alloc(layout) } as *mut MaybeUninit<T>;
let buf = NonNull::new(raw).expect("allocation failed");
```

Never call `alloc` with a zero-size layout — the result is implementation-defined (LLVM may return a non-null dangling pointer). Guard against `capacity == 0` before computing the layout.

**Writing into a slot — always use `ptr::write`:**

```rust
// SAFETY: `self.head < self.cap`, so the offset is in-bounds of the allocation.
//         The slot is currently uninitialised (tracked by self.len), so we are
//         writing into it for the first time — no previous value to drop.
unsafe {
    self.buf.as_ptr().add(self.head).write(MaybeUninit::new(val));
}
self.head = (self.head + 1) % self.cap;
self.len += 1;
```

Do **not** use `*ptr = val`. The assignment operator calls `drop` on the previous value before overwriting it. On uninitialized bytes, that drop reads garbage — undefined behaviour. `ptr::write` copies the bits without running any destructor.

**Reading from a slot — `ptr::read` + `assume_init`:**

```rust
// SAFETY: `self.tail < self.cap` and the slot at `self.tail` holds a fully
//         initialised T (guaranteed by `self.len > 0` pre-check above).
let val = unsafe {
    self.buf.as_ptr().add(self.tail).read().assume_init()
};
self.tail = (self.tail + 1) % self.cap;
self.len -= 1;
Some(val)
```

`ptr::read` performs a *bitwise copy* out of the slot — ownership transfers to `val`, but the original bytes are left in place (the slot is logically uninitialised from your bookkeeping's perspective). Never call `.assume_init()` on a slot you have not confirmed is live.

**Drop implementation — order matters:**

```rust
impl<T> Drop for RingBuffer<T> {
    fn drop(&mut self) {
        // Step 1: drop all live elements
        for i in 0..self.len {
            let idx = (self.tail + i) % self.cap;
            unsafe {
                // SAFETY: indices [tail, tail+len) mod cap hold valid T values.
                self.buf.as_ptr().add(idx).drop_in_place();
            }
        }
        // Step 2: deallocate the backing memory
        let layout = Layout::array::<MaybeUninit<T>>(self.cap).unwrap();
        unsafe {
            // SAFETY: `self.buf` was allocated with this exact layout in `new`.
            dealloc(self.buf.as_ptr() as *mut u8, layout);
        }
    }
}
```

If you skip step 1: every `String`, `Vec`, `Box`, or other owned `T` in the buffer leaks its heap allocation. If you do step 2 before step 1: `drop_in_place` reads through a dangling pointer — use-after-free. The order is non-negotiable.

---

#### Key Pitfalls & Security/Correctness Gotchas

- **`*ptr = val` runs the destructor of the previous value.** In uninitialised slots this means `drop`ing arbitrary bytes as if they were a valid `T`. For `String` this will call `free()` on a garbage pointer. Always use `ptr::write(ptr, val)` for the first write into a slot.

- **`ptr::read` is a bitwise copy, not a destructive move.** After you read a `T` out of a slot, the original bits are still there. If your `Drop` implementation later calls `drop_in_place` on that index (because it thinks `len` is still positive), you get a double-free. Your `len` counter must reflect the true number of *live* elements at all times.

- **`dealloc` must receive the exact same `Layout` as `alloc`.** If you compute the layout differently at deallocation time (e.g. you round up capacity), the allocator sees a mismatched layout and the behaviour is undefined. Use the same `capacity` field you stored at construction.

- **Zero-capacity is a footgun.** `Layout::array::<T>(0)` succeeds and produces a zero-size layout. `alloc` with a zero-size layout is implementation-defined — the allocator may return a dangling non-null pointer or null. Neither outcome is safe to `add(n)` into. Panic on `capacity == 0` at the start of `new`.

- **`unsafe impl Send/Sync` silences the compiler permanently.** If you later add interior mutability (`UnsafeCell`, `Cell`, `AtomicPtr`) to the struct, the `Sync` impl may become unsound. Re-visit these impls whenever the struct's fields change.

- **Forgetting `ManuallyDrop` when implementing `IntoIterator`.** If you ever add an owned iterator that yields `T` by moving out of slots, you must prevent `Drop for RingBuffer` from dropping those same elements a second time (e.g. by wrapping `self` in `ManuallyDrop` once you have moved ownership).

---

#### Bonus Curiosity (Optional — if you finish early)

- **`rtrb` crate**: A production SPSC ring buffer in Rust (~300 lines). It achieves lock-freedom by placing `head` and `tail` in separate cache lines (avoiding false sharing) and using `Relaxed` atomics. Its source is a masterclass in documented unsafe Rust — every `unsafe` block is precisely justified.

- **Miri**: Run `cargo +nightly miri run` on your solution. Miri is a Rust interpreter that dynamically enforces the stacked borrows memory model and will surface use-after-frees, uninitialized reads, and aliasing violations that the static borrow checker cannot see. It is the gold standard for validating unsafe Rust before production.

- **Linux `io_uring`**: The kernel exposes two ring buffers (SQ ring and CQ ring) directly into userspace memory via `mmap`. The producer/consumer split you implement here is structurally identical — the same modulo-index arithmetic, the same head/tail cursors — just with `AtomicU32` indices and the kernel as the consumer on one side.

---

### Research Guidance

- `std::mem::MaybeUninit` — read the section "Initializing an array element-by-element"; it is precisely this use case.
- `std::alloc` module docs — understand the `alloc`/`dealloc` contract and the requirement that `Layout` must match exactly between the two calls.
- `std::ptr::NonNull` docs — pay attention to `as_ptr()`, `NonNull::new` vs `NonNull::new_unchecked`, and the niche optimisation note.
- The Rustonomicon (nomicon.rs), chapter "Working with Uninitialized Memory" — the authoritative source on `MaybeUninit` use patterns in unsafe Rust.
- `std::ptr::drop_in_place` docs — understand what it does versus a plain `ptr::read` and why order matters in `Drop` implementations.

---

### Topics Covered
`unsafe Rust`, `raw pointers`, `NonNull<T>`, `MaybeUninit<T>`, `std::alloc`, `ptr::read`, `ptr::write`, `drop_in_place`, `Send`/`Sync` impls, ring buffer data structure, safety invariants, stacked borrows

### Completion Criteria
1. `cargo build` on stable Rust produces **zero warnings** in both debug and `--release` profiles.
2. `cargo run` produces the expected output exactly: `Some("alpha")`, `Some("beta")`, then `gamma`, `delta`, `epsilon` on separate lines, then `None`.
3. Every `unsafe` block in `src/lib.rs` is preceded by a `// SAFETY:` comment that identifies the invariant making it sound.
4. The `Drop` implementation correctly drops all live `T` values and then deallocates the backing buffer (verify by wrapping a counter in a newtype that prints on drop — confirm the count matches the number of pushed-but-not-popped elements).
5. `unsafe impl Send` and `unsafe impl Sync` are present, each with a comment stating why they are sound.
6. The wrap-around case works correctly: after pushing and popping enough elements to cycle `head` and `tail` past `cap` at least once, FIFO order is preserved throughout.

### Difficulty Estimate
~20 min (fluent in unsafe Rust) | ~75 min (researching from scratch)

### Category
Rust-Specific
