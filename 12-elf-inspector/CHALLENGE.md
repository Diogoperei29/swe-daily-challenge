## Challenge [12] — ELF64 Binary Inspector

### Language
C (C11)

### Description
You will build a minimal `readelf`-like tool from scratch in C. No libelf. No libbfd. No external dependencies. Just `<elf.h>`, `mmap`, pointer arithmetic, and your own parsing logic.

Given a path to any ELF64 binary on Linux, your tool must:

1. Validate the ELF magic bytes, class, and data encoding — reject anything that is not a valid little-endian ELF64 file.
2. Print all **section headers** — resolving each name from `.shstrtab`, along with type, file offset, size, and flags.
3. Print all **program headers** (segments) — type, virtual address, file offset, file size, memory size, and a human-readable flags string (`r-x`, `rw-`, etc.).
4. Walk the `.symtab` section and print every **global symbol** — name (resolved from `.strtab`), binding, type, value (address), and size.

You have a scaffold below that provides the boilerplate types and helper stubs — your job is to fill in the implementation.

```c
// main.c — elf_inspect
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *sh_type_str(uint32_t type) {
    switch (type) {
        case SHT_NULL:     return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB:   return "SYMTAB";
        case SHT_STRTAB:   return "STRTAB";
        case SHT_RELA:     return "RELA";
        case SHT_HASH:     return "HASH";
        case SHT_DYNAMIC:  return "DYNAMIC";
        case SHT_NOTE:     return "NOTE";
        case SHT_NOBITS:   return "NOBITS";
        case SHT_REL:      return "REL";
        case SHT_DYNSYM:   return "DYNSYM";
        default:           return "OTHER";
    }
}

static const char *pt_type_str(uint32_t type) {
    switch (type) {
        case PT_NULL:    return "NULL";
        case PT_LOAD:    return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP:  return "INTERP";
        case PT_NOTE:    return "NOTE";
        case PT_PHDR:    return "PHDR";
        case PT_TLS:     return "TLS";
        default:         return "OTHER";
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf-binary>\n", argv[0]);
        return 1;
    }

    // TODO: open(), fstat(), mmap() the file read-only

    // TODO: validate ELF magic (\x7fELF), class (ELFCLASS64), encoding (ELFDATA2LSB)

    // TODO: print section headers
    //   - locate shstrtab via ehdr->e_shstrndx
    //   - iterate e_shnum sections
    //   - resolve sh_name as offset into shstrtab data

    // TODO: print program headers
    //   - iterate e_phnum program headers
    //   - decode p_flags as a "rwx" string

    // TODO: find SHT_SYMTAB section, use sh_link to find associated strtab
    //   - iterate sh_size / sh_entsize symbols
    //   - print only globals (STB_GLOBAL)

    // TODO: munmap + close

    return 0;
}
```

---

### Background & Teaching

#### Core Concepts

The **ELF (Executable and Linkable Format)** is the standard binary format on Linux, embedded Linux ECUs, and most Unix-like systems. Every executable, shared library, and relocatable object file you encounter is an ELF file. Understanding its layout is foundational for reverse engineering, security auditing, debugger development, linker scripting, and bootloader verification — all directly relevant to automotive embedded/security work.

**The four-part layout of an ELF64 file:**

```
┌──────────────────────────────┐
│  ELF Header  (64 bytes)      │  ← always at offset 0
├──────────────────────────────┤
│  Program Header Table        │  ← used by OS loader / kernel
│  (array of Elf64_Phdr)       │
├──────────────────────────────┤
│  Sections / raw data         │  ← .text, .data, .rodata, etc.
├──────────────────────────────┤
│  Section Header Table        │  ← used by linker & debugger
│  (array of Elf64_Shdr)       │
└──────────────────────────────┘
```

Sections and segments are two **different views** of the same binary data. Sections are for the **linker** (fine-grained: `.text`, `.data`, `.rodata`…). Segments are for the **OS loader** (coarse-grained: one `PT_LOAD` for code, one for data). The section header table can be entirely stripped from a deployed binary and the OS will still load it correctly.

---

**The ELF Header (`Elf64_Ehdr`):**

The first 16 bytes are `e_ident`:

| Offset | Field | Meaning |
|--------|-------|---------|
| 0–3 | Magic | `\x7f E L F` |
| 4 | Class | `ELFCLASS64` = 2 (64-bit) |
| 5 | Data | `ELFDATA2LSB` = 1 (little-endian) |
| 6 | Version | Always 1 |
| 7 | OS/ABI | 0 = System V (most Linux binaries) |
| 8–15 | Padding | zeroes |

Key scalar fields after `e_ident`:
- `e_type`: `ET_EXEC` (executable), `ET_DYN` (shared lib / PIE), `ET_REL` (.o file)
- `e_machine`: `EM_X86_64` = 62
- `e_entry`: virtual address of program entry point
- `e_phoff` / `e_phnum` / `e_phentsize`: location, count, and size of program headers
- `e_shoff` / `e_shnum` / `e_shentsize`: location, count, and size of section headers
- `e_shstrndx`: **index** of the section that contains section-name strings (`.shstrtab`)

---

**Section Headers (`Elf64_Shdr`) and the name string table:**

Each section header is a struct with these important fields:

```c
typedef struct {
    Elf32_Word sh_name;      // offset into .shstrtab — NOT a pointer, an offset!
    Elf32_Word sh_type;      // SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, ...
    Elf64_Xword sh_flags;    // SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR
    Elf64_Addr sh_addr;      // virtual address (0 for non-loadable sections)
    Elf64_Off  sh_offset;    // file offset where this section's data lives
    Elf64_Xword sh_size;     // section size in bytes
    Elf32_Word sh_link;      // for SYMTAB: index of associated string table section
    Elf32_Word sh_info;      // for SYMTAB: index of first *global* symbol
    Elf64_Xword sh_entsize;  // size of each record (for fixed-entry sections)
} Elf64_Shdr;
```

**Section names are indirected through `.shstrtab`.** The field `e_shstrndx` gives you the index of the section whose raw data is a blob of null-terminated strings (e.g., `"\0.text\0.data\0.bss\0..."`). Each `sh_name` is a byte offset into this blob — not an index, not a pointer. To get a section's name: `shstrtab_blob + section->sh_name`.

---

**Program Headers (`Elf64_Phdr`) — the loader's view:**

Program headers describe **loadable segments and runtime metadata**:

- `PT_LOAD`: a region the OS will `mmap` into the process. Usually two: one `r-x` (code) and one `rw-` (data). `p_filesz` bytes are read from the file; if `p_memsz > p_filesz`, the extra bytes are zeroed (this is how `.bss` works).
- `PT_DYNAMIC`: the dynamic linker reads this to find `DT_NEEDED`, symbol tables, etc.
- `PT_INTERP`: path to the ELF interpreter (`/lib64/ld-linux-x86-64.so.2`), stored as a null-terminated string at `p_offset`.
- `PT_GNU_STACK`: `p_flags` encodes whether the stack should be executable. A missing `X` bit here is the `NX` mitigation.
- `PT_GNU_RELRO`: a segment that becomes read-only *after* dynamic linking. Prevents GOT overwrite attacks.

Flags bitmask: `PF_R` = 4, `PF_W` = 2, `PF_X` = 1. Format as `rwx` / `r-x` / `rw-`.

---

**The Symbol Table (`.symtab`):**

`.symtab` is an array of fixed-size `Elf64_Sym` entries:

```c
typedef struct {
    Elf32_Word    st_name;    // offset into .strtab
    unsigned char st_info;   // encodes binding and type:
                             //   binding = st_info >> 4
                             //   type    = st_info & 0xf
    unsigned char st_other;  // visibility (usually 0 = STV_DEFAULT)
    Elf16_Section st_shndx;  // section this symbol is defined in
    Elf64_Addr    st_value;  // address (in executables) or offset (in .o)
    Elf64_Xword   st_size;   // size in bytes (0 if unknown)
} Elf64_Sym;
```

**Binding** (`st_info >> 4`):
- `STB_LOCAL` = 0 — visible only within the translation unit
- `STB_GLOBAL` = 1 — externally visible (functions, global variables)
- `STB_WEAK` = 2 — like global but overridable; also used for undefined weak refs

**Type** (`st_info & 0xf`):
- `STT_NOTYPE` = 0, `STT_OBJECT` = 1 (variable), `STT_FUNC` = 2 (function), `STT_SECTION` = 3, `STT_FILE` = 4

The `.symtab` section header's `sh_link` field gives the **section index of its associated string table** (`.strtab`). To resolve `sym->st_name`: `strtab_data + sym->st_name`.

The number of symbol entries: `shdr.sh_size / shdr.sh_entsize`.

**Important:** many deployed/stripped binaries have `.symtab` removed. Only `.dynsym` (`SHT_DYNSYM`) survives stripping, as it is needed for dynamic linking at runtime. Your tool must handle the absent `.symtab` case gracefully.

---

**The mmap approach:**

`mmap` the entire file once with `MAP_PRIVATE | PROT_READ`. Then every ELF structure is just a pointer cast:

```c
uint8_t *base = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
Elf64_Ehdr *ehdr  = (Elf64_Ehdr *)base;
Elf64_Shdr *shdrs = (Elf64_Shdr *)(base + ehdr->e_shoff);
Elf64_Phdr *phdrs = (Elf64_Phdr *)(base + ehdr->e_phoff);
char *shstrtab    = (char *)(base + shdrs[ehdr->e_shstrndx].sh_offset);
```

No manual `read()` loops, no `fseek`, no allocations for the raw file data.

---

#### How to Approach This in C

1. **Open and map**: `open()` → `fstat()` to get size → `mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0)`. You can `close(fd)` immediately after `mmap` succeeds; the mapping persists.

2. **Validate**: check `base[0..3]` for `\x7fELF`, then `e_ident[EI_CLASS] == ELFCLASS64`, then `e_ident[EI_DATA] == ELFDATA2LSB`. Print to `stderr` and exit non-zero on failure.

3. **Section headers**: `Elf64_Shdr *shdrs = (Elf64_Shdr *)(base + ehdr->e_shoff)`. Resolve `.shstrtab` first (`shdrs[ehdr->e_shstrndx]`), then loop from `i = 0` to `ehdr->e_shnum`. For each: `shdrs[i].sh_name` is the offset into `shstrtab_data`.

4. **Program headers**: `Elf64_Phdr *phdrs = (Elf64_Phdr *)(base + ehdr->e_phoff)`. Loop 0 to `e_phnum`. Decode `p_flags` with a 3-character buffer: `flags[0] = (p->p_flags & PF_R) ? 'r' : '-'`, etc.

5. **Symbol table**: Iterate sections looking for `sh_type == SHT_SYMTAB`. When found:
   - `sh_link` → index of the `.strtab` section → `char *strtab = base + shdrs[sh_link].sh_offset`
   - `count = shdr.sh_size / shdr.sh_entsize`
   - cast to `Elf64_Sym *syms = (Elf64_Sym *)(base + shdr.sh_offset)`
   - For each sym: filter `ELF64_ST_BIND(sym.st_info) == STB_GLOBAL`, then print

   Use the macro `ELF64_ST_BIND(info)` and `ELF64_ST_TYPE(info)` — they are defined in `<elf.h>`.

6. **Cleanup**: `munmap(base, size)`.

**Do NOT** use `SHT_STRTAB` section names to identify `.strtab` — use `sh_link` from the `.symtab` header. There can be multiple `SHT_STRTAB` sections (`.shstrtab`, `.dynstr`, `.strtab`…); `sh_link` is the authoritative pointer.

---

#### Key Pitfalls & Security/Correctness Gotchas

- **`sh_name` is a string table offset, not a section index.** A very common confusion. `shdrs[i].sh_name` is added to the *data* of `.shstrtab`, not used to index `shdrs[]`. Forgetting this produces gibberish names or a segfault.

- **`e_shstrndx` can be `SHN_XINDEX` (0xffff).** When an ELF has more than 65279 sections (rare but valid), the actual string table index is stored in `shdrs[0].sh_link`. Check for this and at minimum print an error rather than silently using the wrong section.

- **Bounds-check every `sh_offset + sh_size` before dereferencing.** A crafted or corrupted ELF can have an offset pointing outside the mapped region. Before treating `base + offset` as valid data, assert `offset + size <= file_size`. This is especially important in automotive/security contexts where you may be parsing untrusted firmware.

- **Stripped binaries silently have no `.symtab`.** If your scan finds no `SHT_SYMTAB` section, print a notice (`"no .symtab found (binary is stripped)"`) and exit with code 0. Do not crash or return an error — absence of a symbol table is normal.

- **`sh_entsize` can be 0 for non-uniform sections** (e.g., `SHT_STRTAB`). Never divide by `sh_entsize` without checking it is nonzero first. For `SHT_SYMTAB` it is always `sizeof(Elf64_Sym)`, but be defensive.

- **`st_value` in a relocatable `.o` is a section-relative offset, not a virtual address.** Only in `ET_EXEC` and `ET_DYN` (with dynamic base) is `st_value` a virtual address. Note this in your output if you want to be precise.

---

#### Bonus Curiosity (Optional — if you finish early)

- **`PT_GNU_RELRO` and full-RELRO vs. partial-RELRO**: Partial-RELRO (the default with `-Wl,-z,relro`) marks the `.got` section read-only after startup but leaves `.got.plt` writable. Full-RELRO (`-Wl,-z,relro,-z,now`) also forces eager binding, making `.got.plt` writable only during the `_start`→`main` window. Understanding this is directly applicable to analyzing automotive ECU binaries for missing hardening flags.

- **DWARF debug information**: The sections `.debug_info`, `.debug_abbrev`, `.debug_line` encode the entire C type system, variable locations, and source-line mapping in a compact form. GDB reads DWARF to let you type `p my_struct`. The `dwarfdump` tool makes this human-readable. Writing a DWARF parser is a natural next step from this challenge.

- **GNU Build ID (`NT_GNU_BUILD_ID`)**: A SHA-1 hash stored in a `PT_NOTE` segment that uniquely identifies a binary independent of path or timestamp. Used by `debuginfod` and crash reporting systems (e.g., `coredumpctl`) to fetch matching debug symbols. Run `readelf -n /bin/ls` to see it. Relevant to embedded forensics when you have a stripped binary from a field-returned ECU.

---

### Research Guidance

- `man 5 elf` — the canonical Linux ELF format reference; read every field of `Elf64_Ehdr`, `Elf64_Shdr`, `Elf64_Phdr`, and `Elf64_Sym`.
- `cat /usr/include/elf.h` — inspect the actual C definitions and all `SHT_*`, `PT_*`, `STB_*`, `STT_*` constants on your system.
- `readelf -S -l -s /bin/ls` — run this and compare the output section-by-section against what your tool prints.
- "Linkers and Loaders" by John R. Levine, Chapter 3 — thorough treatment of ELF format, segments vs. sections, and the linking process.
- `objdump -d -t /bin/ls` — cross-check your symbol table output against a known-good tool.

---

### Topics Covered
ELF64 format, section headers, program headers, symbol table, `.shstrtab`, `.strtab`, `mmap`, binary parsing, `<elf.h>`, `Elf64_Sym`, pointer arithmetic, bounds checking

### Completion Criteria
1. `gcc -Wall -Wextra -o elf_inspect main.c` compiles with zero warnings.
2. Running `./elf_inspect /bin/ls` prints all section headers with correct names (e.g., `.text`, `.data`, `.rodata`), types, file offsets, and sizes matching `readelf -S /bin/ls`.
3. Running `./elf_inspect /bin/ls` prints all program headers with type, virtual address, file offset, file/mem sizes, and a human-readable flags string (`r-x`, `rw-`, etc.), matching `readelf -l /bin/ls`.
4. Running `./elf_inspect` on a non-stripped binary (compile your own small test program with no `-s` flag) prints at least 5 global symbol entries with correct names and addresses, verified against `readelf -s`.
5. Running `./elf_inspect /bin/ls` (stripped binary) prints `"no .symtab found"` and exits with code 0.
6. Running `./elf_inspect /etc/passwd` prints an error message to `stderr` and exits with a non-zero code.

### Difficulty Estimate
~20 min (knowing the domain) | ~60 min (researching from scratch)

### Category
SYSTEMS PROGRAMMING
