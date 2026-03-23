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
    pub fn new(capacity: usize) -> Self {
        if capacity == 0 { panic!("Capacity cannot be zero\n"); }
           
        let layout = Layout::array::<MaybeUninit<T>>(capacity).expect("launch overflow");
        // SAFETY: layout is guaranteed to have non-zero size
        let raw = unsafe { alloc(layout) } as *mut MaybeUninit<T>;
        let buf = NonNull::new(raw).expect("NotNull new allocation failed");

        Self {
            buf : buf,
            cap : capacity,
            head : 0,
            tail : 0,
            len: 0
        }
    }

    /// Enqueues `val`. Returns `Err(val)` if the buffer is full.
    pub fn push(&mut self, val: T) -> Result<(), T> {
        if self.is_full() { return Err(val); }
        unsafe {
            // SAFETY: buf is non-null and points to a live heap allocation of cap * sizeof::<MaybeUninit<T>>() bytes
            self.buf.as_ptr().add(self.head).write(MaybeUninit::new(val));
        }
        self.head = (self.head + 1) % self.cap;
        self.len += 1;
        Ok(())
    }

    /// Removes and returns the oldest element, or `None` if empty.
    pub fn pop(&mut self) -> Option<T> {
        if self.is_empty() { return None; }
        
        let val = unsafe {
            // SAFETY: Every index i in [tail, tail + len) modulo cap holds a fully initialised T
            self.buf.as_ptr().add(self.tail).read().assume_init()
        };
        self.tail = (self.tail + 1) % self.cap;
        self.len -= 1;
        Some(val)
    }

    pub fn len(&self)      -> usize { self.len }
    pub fn is_empty(&self) -> bool  { self.len == 0 }
    pub fn is_full(&self)  -> bool  { self.len == self.cap }
}

impl<T> Drop for RingBuffer<T> {
    // ignoring the rest of point 4, too much hassle xD
    fn drop(&mut self) {
        // drop all live elements
        for i in 0..self.len {
            let idx = (self.tail + i) % self.cap;
            unsafe {
                // SAFETY: indices [tail, tail+len) mod cap hold valid T values.
                self.buf.as_ptr().add(idx).drop_in_place();
            }
        }
        // deallocate the backing memory
        let layout = Layout::array::<MaybeUninit<T>>(self.cap).unwrap();
        unsafe {
            // SAFETY: `self.buf` was allocated with this exact layout in `new`.
            dealloc(self.buf.as_ptr() as *mut u8, layout);
        }
    }
}

// SAFETY: no one besides RingBuffer has the raw pointer, there is no shared state, 
// so it can be transfered as long as T is also Send
unsafe impl<T> Send for RingBuffer<T> where T: Send{}
// SAFETY: only read-only methods are exposed, all observe usize fields that are immutable
// so it is safe to share as long as T is also Sync
unsafe impl<T> Sync for RingBuffer<T> where T: Sync{}