## Challenge [008] — LSM-Tree: MemTable, SSTable Flush, and Point Queries

### Language
C++ (C++17 or later)

### Description
Log-Structured Merge-Trees (LSM trees) power some of the most widely deployed
storage engines in the world: RocksDB (used in Meta, Kafka, LinkedIn), LevelDB
(used in Chrome's IndexedDB), Cassandra, and many others. Unlike B-trees, which
update data in-place, LSM trees make all writes sequential and batch-compact data
in the background — a fundamentally different tradeoff that converts random writes
into sequential writes at the cost of more complex reads.

Your task: implement a minimal but correct **single-level LSM-tree storage engine**
in C++. It must support:

1. A **MemTable** — an in-memory sorted key-value store (backed by `std::map`)
2. An **SSTable flush** — when the MemTable exceeds a threshold (4 entries), write
   it to a numbered binary file on disk and clear the MemTable
3. A **point query** — look up a key by checking the MemTable first, then scanning
   SSTable files on disk newest-first
4. **Tombstone deletes** — `del(key)` inserts a special deletion marker that
   suppresses older values found in earlier SSTables

Use the scaffold below as your starting point (`08/main.cpp`):

```cpp
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr size_t MEMTABLE_FLUSH_THRESHOLD = 4;
static const std::string TOMBSTONE = "\x00TOMBSTONE\x00";

struct SSTableEntry {
    std::string key;
    std::string value;  // equals TOMBSTONE sentinel if deleted
};

class LSMTree {
public:
    LSMTree(const std::string& dataDir) : dataDir_(dataDir), sstableCount_(0) {
        fs::create_directories(dataDir_);
        // TODO: on startup, discover existing SSTable files
    }

    void put(const std::string& key, const std::string& value) {
        memtable_[key] = value;
        maybeFlush();
    }

    void del(const std::string& key) {
        // TODO: insert a tombstone into the MemTable
    }

    std::optional<std::string> get(const std::string& key) {
        // TODO: check MemTable, then SSTables newest-first
        return std::nullopt;
    }

private:
    void maybeFlush() {
        if (memtable_.size() >= MEMTABLE_FLUSH_THRESHOLD) {
            flush();
        }
    }

    void flush() {
        // TODO: write memtable to a new SSTable file, clear memtable
    }

    void writeSSTable(const std::map<std::string, std::string>& data,
                      const std::string& path) {
        // TODO: write a binary SSTable:
        //   for each entry: [key_len: uint32_t][key bytes][val_len: uint32_t][val bytes]
    }

    std::vector<SSTableEntry> readSSTable(const std::string& path) {
        // TODO: read entries back in the same format
        return {};
    }

    std::map<std::string, std::string> memtable_;
    std::string dataDir_;
    int sstableCount_;
    std::vector<std::string> sstablePaths_;  // oldest at [0], newest at back
};

int main() {
    LSMTree lsm("./lsm_data");

    lsm.put("apple",  "fruit");
    lsm.put("banana", "yellow");
    lsm.put("cherry", "red");
    lsm.put("date",   "sweet");        // triggers flush #1

    lsm.put("elderberry", "dark");
    lsm.put("fig",        "chewy");
    lsm.put("grape",      "vine");
    lsm.put("apple",      "v2");       // overwrites apple; triggers flush #2

    lsm.del("banana");                 // tombstone in memtable

    // Expected output:
    std::cout << "apple: "      << lsm.get("apple").value_or("NOT FOUND")      << "\n"; // v2
    std::cout << "banana: "     << lsm.get("banana").value_or("NOT FOUND")     << "\n"; // NOT FOUND
    std::cout << "cherry: "     << lsm.get("cherry").value_or("NOT FOUND")     << "\n"; // red
    std::cout << "date: "       << lsm.get("date").value_or("NOT FOUND")       << "\n"; // sweet
    std::cout << "elderberry: " << lsm.get("elderberry").value_or("NOT FOUND") << "\n"; // dark
    std::cout << "missing: "    << lsm.get("missing").value_or("NOT FOUND")    << "\n"; // NOT FOUND
}
```

---

### Background & Teaching

#### Core Concepts

**Why LSM Trees Exist**

Traditional B-tree storage engines (SQLite, PostgreSQL, InnoDB) store data in a
balanced tree on disk. Every update is an *in-place* write: find the correct page,
modify it, flush it. This produces random writes — small, scattered page-level I/Os
that saturate the disk seek queue on HDDs and cause significant write amplification
on SSDs (a small update rewrites an entire 4KB page, which the SSD must erase-before-
write at block granularity, typically 256KB–1MB).

LSM trees make a different tradeoff: **turn all writes into sequential writes** by
staging them through an in-memory buffer and flushing immutable sorted files to disk.
Sequential writes are dramatically faster on both HDDs (no seek) and SSDs (larger
writes fill erase blocks efficiently, reducing wear). The cost is that reads may need
to consult multiple files, and the system must periodically merge files to prevent
read performance from degrading unboundedly.

**The Three-Layer Mental Model**

A minimal single-level LSM tree has these logical components:

1. **MemTable** — an in-memory sorted dictionary. All writes land here first. Because
   it is in RAM, writes are O(log n) and very fast. In production engines, this is often
   a skip list (LevelDB) or a concurrent skip list (RocksDB). For this challenge, a
   `std::map<string, string>` is correct.

2. **Flush** — when the MemTable exceeds a size threshold, its entire contents are
   written to a new, immutable, sorted file on disk (an SSTable). The MemTable is then
   cleared. This write is a single sequential pass through the sorted map — no random
   seeks. In production, the MemTable is first made "immutable" (swapped atomically for
   a fresh one) so writes can continue while the flush happens in the background.

3. **SSTables (Sorted String Tables)** — immutable, sorted, binary files on disk. Each
   key-value pair is stored in sorted order. Files are never modified after creation.
   Newer SSTables have logically higher priority than older ones.

**SSTable Binary Format**

The on-disk format for a minimal SSTable is just a sequence of length-prefixed records:

```
[key_len: uint32_t LE][key bytes][val_len: uint32_t LE][val bytes]
```

Repeated for every entry in sorted key order. No padding, no header. Production
SSTables add a data block / index block / filter block / footer layout (similar to
an SSTable in LevelDB), but the flat format is sufficient for correctness here.

**Reading: The Lookup Cascade**

To look up a key, perform a cascade in order from newest data to oldest:

1. Check the MemTable — O(log n), in-memory, fastest
2. If not found, scan each SSTable from **newest to oldest** (most recent flush first)

The "newest-first" ordering is a correctness requirement. If you flushed
`apple → fruit` in SSTable #1, then later wrote `apple → v2`, that update is in
SSTable #2 (or still in the MemTable). You must return `v2` — you will only get the
right answer if you find SSTable #2 before SSTable #1.

**Tombstones: Logical Deletion**

SSTable files are immutable. You cannot remove a key from a file you already wrote.
Instead, deletion is implemented by writing a special **tombstone** record into the
MemTable:

```
del("banana")  →  memtable_["banana"] = TOMBSTONE_SENTINEL
```

The tombstone is treated as a normal record and eventually flushed to disk. During a
lookup, if the newest value found for a key is the tombstone sentinel, the key is
considered deleted and the lookup returns "not found" — even if an older SSTable
contains the original `banana → yellow`. The tombstone in the newer file wins because
you scan newest-first.

This is why tombstones cannot be discarded from SSTables immediately. The old value
still exists in an older SSTable. Only during **compaction** (merging N SSTables into
one) can a tombstone be safely dropped, and only if no older SSTable contains the
original key.

**Compaction (Background — Not Implemented Here)**

As SSTables accumulate, read performance degrades (more files to scan) and tombstones
waste space. A real LSM engine periodically runs **compaction**: merge N SSTables by
performing a merge-sort across their sorted sequences, emitting only the newest value
per key, and dropping tombstones for keys that have no remaining older version. This
process is responsible for LSM trees' write amplification — data is written once to
the MemTable, once per flush, and once per compaction level. RocksDB's leveled
compaction and tiered (universal) compaction are the two dominant strategies.

**Persistence Across Restarts**

On startup, the engine must rediscover existing SSTable files from the data directory
and register them in age order. Without this, a restart would appear to have lost all
data already on disk. SSTable files named with a zero-padded numeric suffix
(`sstable_0000.sst`, `sstable_0001.sst`, ...) sort lexicographically in age order,
making discovery straightforward with `std::filesystem::directory_iterator`.

#### How to Approach This in C++

**MemTable**: Use `std::map<std::string, std::string>`. It maintains sorted order,
which matches the SSTable invariant. For writes, use `operator[]`. For reads, use
`find()` — **never** `operator[]` in a read path, as it creates a default-initialized
empty-string entry if the key doesn't exist.

**Binary I/O**: Open files with `std::ios::binary`. Write a `uint32_t` length prefix
before each string:

```cpp
uint32_t len = static_cast<uint32_t>(s.size());
out.write(reinterpret_cast<const char*>(&len), sizeof(len));
out.write(s.data(), static_cast<std::streamsize>(s.size()));
```

To read back:

```cpp
uint32_t len = 0;
in.read(reinterpret_cast<char*>(&len), sizeof(len));
if (!in) break;  // EOF or error — clean exit
std::string s(len, '\0');
in.read(s.data(), len);
```

**File Naming**: Zero-pad the counter to 4 digits for lexicographic sortability:
```cpp
// e.g., "lsm_data/sstable_0000.sst"
std::ostringstream oss;
oss << dataDir_ << "/sstable_" << std::setw(4) << std::setfill('0') << sstableCount_ << ".sst";
```

**SSTable Discovery on Startup**: Use `std::filesystem::directory_iterator` to list
`*.sst` files in `dataDir_`. Collect them into a `std::vector<std::string>`, sort
lexicographically (which gives age order due to the naming scheme), push them into
`sstablePaths_`, and set `sstableCount_` to the number of discovered files. This
prevents counter collisions on the next flush.

**Newest-First Scan**: With `sstablePaths_` stored oldest-at-front, iterate in
reverse:

```cpp
for (int i = static_cast<int>(sstablePaths_.size()) - 1; i >= 0; --i) {
    for (const auto& entry : readSSTable(sstablePaths_[i])) {
        if (entry.key == key) {
            if (entry.value == TOMBSTONE) return std::nullopt;
            return entry.value;
        }
    }
}
```

Stop (return) as soon as you find the key in any file — the first hit is the newest.

**Flush Procedure** (in order):
1. Call `writeSSTable(memtable_, path)` — this is the sequential write
2. `sstablePaths_.push_back(path)` — register in age order
3. `memtable_.clear()`
4. `++sstableCount_`

#### Key Pitfalls & Security/Correctness Gotchas

- **Scanning SSTables oldest-first instead of newest-first** will cause tombstones
  and overwritten values to be silently ignored. The data returned will often be the
  *oldest* value, not the newest. Always iterate from the back of `sstablePaths_`.

- **Using `operator[]` in the read path** on `std::map` inserts an empty-string entry
  for every key you query. This corrupts the MemTable's contents and state (size grows,
  phantom keys exist). Use `find()` everywhere except for writes.

- **sstableCount_ not restored on startup**. If the program previously flushed 2 files
  (`.../sstable_0000.sst` and `.../sstable_0001.sst`) and you restart with
  `sstableCount_ = 0`, the next flush will overwrite `sstable_0000.sst`, silently
  destroying data. After directory discovery, set `sstableCount_` to the count of
  discovered files.

- **Forgetting `std::ios::binary` on Windows**. Without the binary flag, `\n` bytes in
  values are translated to `\r\n` on write and `\r\n` to `\n` on read, corrupting
  binary length-prefix fields and producing garbage reads. Always open with
  `std::ios::binary` for raw byte storage.

- **Allocating unbounded memory on a corrupt length field**. If a `.sst` file is
  truncated or the length field is corrupted, `uint32_t len` could be a huge number.
  `std::string(len, '\0')` followed by a `read()` will likely segfault or OOM. Add a
  sanity check: `if (len > 1024 * 1024) break;` — no key or value in this challenge
  exceeds 1MB.

- **Not flushing the stream before closing**. `std::ofstream` buffers writes. If you
  let it go out of scope without an explicit `out.flush()` or `out.close()`, data may
  not reach disk before the next `readSSTable()` call reads the same file. Explicitly
  call `out.close()` or ensure the `ofstream` destructor runs before you push the
  path into `sstablePaths_`.

#### Bonus Curiosity (Optional — if you finish early)

- **Bloom filters on SSTables**: Before scanning any SSTable file, a real engine first
  checks a per-file Bloom filter to answer "this key *definitely* does not exist in
  this file" in O(1). False positives (saying the key might be there when it isn't)
  cost one unnecessary file scan. False negatives are impossible by design. Look up
  RocksDB's "Blocked Bloom Filter" — it's a cache-line-aligned variant that makes
  filter probes ~2x faster than a standard Bloom filter by exploiting spatial locality.

- **The 1992 LFS paper**: Rosenblum & Ousterhout's "The Design and Implementation of
  a Log-Structured File System" (SOSP '91) is the direct intellectual ancestor of LSM
  trees. The paper's observation that free-space cleaning (defragmenting a log-
  structured FS) is structurally identical to LSM compaction is remarkable. Worth 30
  minutes of reading if you want to understand *why* this design works.

- **WiscKey (FAST '16)**: WiscKey separates keys from values — it stores only keys in
  the LSM tree and puts large values in a separate append-only "vLog". This eliminates
  value re-writing during compaction (which dominates write amplification for large
  values) at the cost of slightly more complex garbage collection. It's a clever paper
  and directly influenced RocksDB BlobDB.

---

### Research Guidance

- "The Log-Structured Merge-Tree (LSM-Tree)" — O'Neil et al., 1996 — the original
  paper; §2.1 describes the basic two-component model that maps directly to this
  challenge
- RocksDB Wiki: "RocksDB Overview" — covers MemTable, immutable MemTable, L0 files,
  level compaction, and write stall triggers; a thorough ground-truth reference
- LevelDB source code: `db/memtable.h` and `table/table_builder.cc` — a clean,
  ~3000-line reference implementation; read `table_builder.cc` to see how a real
  SSTable is written block by block
- `man 3 fwrite` and `cppreference std::basic_fstream` — understand why `ios::binary`
  matters and what "text mode" does to byte streams on different platforms
- RocksDB Wiki: "Leveled Compaction" — explains why L0 SSTables have overlapping key
  ranges (they come directly from MemTable flushes) while L1+ SSTables are
  non-overlapping, and what triggers compaction at each level

### Topics Covered
LSM tree, MemTable, SSTable, sorted string table, binary file I/O, tombstone
deletion, log-structured storage, point query cascade, newest-first scan, C++17
filesystem, storage engine internals

### Completion Criteria

1. The program compiles with `-Wall -Wextra -std=c++17` and zero warnings.
2. Running `./lsm` prints exactly:
   ```
   apple: v2
   banana: NOT FOUND
   cherry: red
   date: sweet
   elderberry: dark
   missing: NOT FOUND
   ```
3. After execution, exactly two `.sst` files exist in `./lsm_data/` (one per flush
   triggered by the 4-entry threshold).
4. Tombstone correctness: `banana` returns `NOT FOUND` even though SSTable #1
   contains `banana → yellow`.
5. Overwrite correctness: `apple` returns `v2` (from SSTable #2 / MemTable) rather
   than `fruit` (from SSTable #1), proving newest-first scan order.
6. Restart safety: running `./lsm` a second time does not crash, does not overwrite
   existing `.sst` files, and `sstableCount_` is initialized past the discovered files.

### Difficulty Estimate
~20 min (knowing LSM trees) | ~60–90 min (researching from scratch)

### Category
DATABASES & STORAGE
