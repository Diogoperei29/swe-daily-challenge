#include <atomic>
#include <optional>
#include <cstddef>
#include <thread>
#include <iostream>
#include <unordered_set>
#include <future>

#define STRESS_TEST_N 10000

template <typename T>
class TreiberStack {
    struct Node {
        T       value;
        Node*   next;
        explicit Node(T v) : value(std::move(v)), next(nullptr) {}
    };

    std::atomic<Node*> head{ nullptr };
    std::atomic<Node*> trash{ nullptr }; // retired nodes; freed in destructor after all threads join

public:
    // Push val onto the stack. Never fails.
    void push(T val) {
        Node* newHead = new Node(val);

        // if head is not newHead->next, exchange fails
        // which means some other thread changed head first
        // retry and update

        Node* oldHead = head.load(std::memory_order_relaxed);  // order of memory operations do not matter here, only need atomic
        do {
            newHead->next = oldHead;
        }
        while (!head.compare_exchange_weak(oldHead, newHead,
                                           std::memory_order_release,    // updating shared data, cannot reorder read/writes after store
                                           std::memory_order_relaxed )); // relaxed on failure: we loop and retry, no sync needed
    }

    // Pop the top element. Returns std::nullopt if empty.
    std::optional<T> pop() {
        Node *oldHead = head.load(std::memory_order_acquire); // pairs with push's release; makes pushed node's fields visible
        Node *newHead;
        do {
            if (oldHead == nullptr) return std::nullopt;
            newHead = oldHead->next;
        } while (!head.compare_exchange_weak(oldHead, newHead,
                                            std::memory_order_acquire,   // synchronize with push's release so node data is readable
                                            std::memory_order_relaxed)); // relaxed on failure: we loop and retry, no sync needed

        T val = std::move(oldHead->value);
        // Retire instead of delete: another thread may still hold 'oldHead' as its
        // own loop variable and read oldHead->next before it sees the CAS failure.
        // Immediate delete would be a use-after-free. We free safely in the destructor.
        Node* old_trash = trash.load(std::memory_order_relaxed);
        do { oldHead->next = old_trash; }
        while (!trash.compare_exchange_weak(old_trash, oldHead,
                                            std::memory_order_release,
                                            std::memory_order_relaxed));
        return val;
    }

    // True iff the stack contains no elements (may race — use only for hints).
    bool empty() const noexcept {
        return (head.load(std::memory_order_relaxed) == nullptr); // only need atomic operation
    }

    // Must not leak. Assumes no concurrent access during destruction.
    ~TreiberStack() {
        Node* cur = head.load(std::memory_order_relaxed);
        while (cur) { Node* next = cur->next; delete cur; cur = next; }
        cur = trash.load(std::memory_order_relaxed);
        while (cur) { Node* next = cur->next; delete cur; cur = next; }
    }
};


void producer (TreiberStack<int>& stack, int n) {
    for(int i = 0 + n*STRESS_TEST_N; i < STRESS_TEST_N + n*STRESS_TEST_N; i++)
        stack.push(i);
}

std::unordered_set<int> consumer(TreiberStack<int>& stack, std::atomic<bool>& done) {
    std::unordered_set<int> cons_set;
    while (!done.load(std::memory_order_relaxed) || !stack.empty()) {
        auto res = stack.pop();
        if (res.has_value())
            cons_set.insert(*res);
    }
    return cons_set;
}



int main() {
    TreiberStack<int> stack;
    std::atomic<bool> done{ false };

    std::thread p1(producer, std::ref(stack), 0);
    std::thread p2(producer, std::ref(stack), 1);
    std::thread p3(producer, std::ref(stack), 2);
    std::thread p4(producer, std::ref(stack), 3);

    auto ret1 = std::async(std::launch::async, consumer, std::ref(stack), std::ref(done));
    auto ret2 = std::async(std::launch::async, consumer, std::ref(stack), std::ref(done));
    auto ret3 = std::async(std::launch::async, consumer, std::ref(stack), std::ref(done));
    auto ret4 = std::async(std::launch::async, consumer, std::ref(stack), std::ref(done));

    p1.join();
    p2.join();
    p3.join();
    p4.join();
    // Signal consumers: all items have been pushed, drain the rest and exit
    done.store(true, std::memory_order_seq_cst);

    std::unordered_set<int> all_sets;
    all_sets.merge(ret1.get());
    all_sets.merge(ret2.get());
    all_sets.merge(ret3.get());
    all_sets.merge(ret4.get());

    const int expected = 4 * STRESS_TEST_N;
    bool ok = (static_cast<int>(all_sets.size()) == expected);
    // Also verify every expected value is actually present
    for (int i = 0; i < expected && ok; ++i)
        ok = all_sets.count(i) == 1;

    if (ok)
        std::cout << "PASS: all " << expected << " values received exactly once\n";
    else
        std::cout << "FAIL: got " << all_sets.size() << " unique values (expected " << expected << ")\n";
}

/*
// ABA ANALYSIS

(a) Exact three-thread interleaving that triggers ABA:

    Stack state: head -> A -> B -> C

    T1 (pop): loads oldHead = A, computes newHead = A->next = B.
              T1 is now SUSPENDED before executing its CAS.

    T2 (pop): completes a pop of A  => head -> B -> C, then frees node A.
    T2 (pop): completes a pop of B  => head -> C,     then frees node B.
    T2 (push): a new push allocates a node — the allocator reuses A's
               freed memory address for the new node. head -> A' -> C.

    T1 resumes:  CAS reads head == A (point address match), succeeds, sets
                 head = B.  But B was freed by T2 — head now points to
                 deallocated memory. Use-after-free / corruption.

(b) Vulnerable operation:
    The CAS in pop():
        head.compare_exchange_weak(oldHead, newHead, ...)
    where newHead = oldHead->next = B (already freed by T2).
    The CAS succeeds because head's current address equals oldHead (A),
    but the "next" captured in newHead is a dangling pointer.

(c) Why the stress test is unlikely to trigger it in practice:
    ABA requires the system allocator to return the exact freed address
    of A which the changes of these coinciding is very low but not zero
*/



