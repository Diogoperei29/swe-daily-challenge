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
        // SAFETY: buf is non-null and points to a live heap allocation of cap * sizeof::<MaybeUninit<T>>() bytes
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
        unsafe {
            self.buf.as_ptr().add(self.head).write(MaybeUninit::new(val));
        }
        self.head = (self.head + 1) % self.cap;
        self.len += 1;
        Ok(())
    }

    /// Removes and returns the oldest element, or `None` if empty.
    pub fn pop(&mut self) -> Option<T> {
        todo!()
    }

    pub fn len(&self)      -> usize { self.len }
    pub fn is_empty(&self) -> bool  { self.len == 0 }
    pub fn is_full(&self)  -> bool  { self.len == self.cap }
}

impl<T> Drop for RingBuffer<T> {
    fn drop(&mut self) {
        todo!()
    }
}
