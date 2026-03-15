## Challenge [007] — JIT: Emit and Execute x86-64 Machine Code at Runtime

### Language
C (primary). No external libraries beyond the C standard library and POSIX APIs.

### Description
You will build a minimal JIT engine in C. Your program allocates a page of
anonymous memory with `mmap`, hand-encodes raw x86-64 instruction bytes into
it, switches the page's protection from writable to executable (W^X), casts
the buffer regions to function pointers, and calls them directly.

You must JIT-compile and execute **two required functions** and one **stretch
goal**:

| Function              | Signature             | Expected result     |
|-----------------------|-----------------------|---------------------|
| `add`                 | `int (int a, int b)`  | `add(3, 4)   == 7`  |
| `mul`                 | `int (int a, int b)`  | `mul(6, 7)   == 42` |
| `fibonacci` (stretch) | `long (long n)`       | `fib(10)     == 55` |

No C arithmetic may be used to compute these values — all computation must
happen inside the JIT-compiled code.

Starter code is in `main.c`. It sets up the skeleton; you fill in the
instruction bytes and the `mprotect` call.

```
$ gcc -Wall -Wextra -o jit main.c && ./jit
add(3,  4)  = 7   [OK]
mul(6,  7)  = 42  [OK]
fib(10)     = 55  [OK]   ← stretch
```

---

### Background & Teaching

This section is the core of the challenge. Read it fully before writing code.

#### Core Concepts

**What is a JIT compiler?**

A Just-In-Time compiler generates native machine code at runtime and executes
it directly. Unlike an interpreter (which walks bytecode or an AST repeatedly)
or an AOT compiler (which emits code before the program runs), a JIT lives
between the two: code is compiled during execution, written into memory, and
jumped into as ordinary native code. Real-world examples include V8
(JavaScript), the HotSpot JVM, LuaJIT, and the Linux eBPF verifier.

In this challenge you implement the most minimal possible JIT: you
hand-encode x86-64 instructions as raw bytes into an `mmap`'d buffer, then
tell the kernel "this memory is executable now" and call into it. There is no
parser, no IR, no register allocator — only byte encoding.

---

**x86-64 instruction encoding**

x86-64 instructions are variable-length byte sequences. For simple
register-to-register operations (no memory access), an instruction usually
consists of three parts:

1. **REX prefix** (optional, one byte, range `0x40`–`0x4F`): extends register
   encoding fields to 64-bit. The most important bit is `REX.W` (bit 3 of the
   prefix byte). `REX.W=1` means "use 64-bit operand size". The prefix byte
   for "64-bit operand, no other extensions" is `0x48`.

   Without a REX.W prefix, most arithmetic instructions default to 32-bit
   operands and **zero-extend** the result into the 64-bit register. This is
   intentional in the ABI: writing `eax` (32-bit) clears the upper 32 bits of
   `rax` (64-bit).

2. **Opcode byte(s)**: identifies the operation.
   - `0x01` — `ADD r/m32, r32` (add register into reg/memory, 32-bit)
   - `0x89` — `MOV r/m32, r32` (move register into reg/memory, 32-bit)
   - `0x0F 0xAF` — `IMUL r32, r/m32` (signed multiply, two-operand)
   - `0xC3` — `RET` (near return)
   - `0xEB` — `JMP rel8` (short unconditional jump, signed 8-bit displacement)
   - `0x7E` — `JLE rel8` (jump if less-or-equal, signed 8-bit displacement)
   - `0x7F` — `JG  rel8`  (jump if greater)

3. **ModRM byte** (one byte, follows most arithmetic/move opcodes):
   encodes the addressing mode and the two register operands.
   It is structured as three fields: `[mod:2][reg:3][rm:3]`.

   - `mod = 0b11` (value 3): both operands are registers (no memory).
   - `reg`: the register in the "source" or "reg" role.
   - `rm`:  the register in the "destination" or "r/m" role.

   Register numbers (same for 32-bit and 64-bit variants):
   | Number | 32-bit name | 64-bit name |
   |--------|-------------|-------------|
   | 0      | `eax`       | `rax`       |
   | 1      | `ecx`       | `rcx`       |
   | 2      | `edx`       | `rdx`       |
   | 6      | `esi`       | `rsi`       |
   | 7      | `edi`       | `rdi`       |

   ModRM byte formula: `(3 << 6) | (reg << 3) | rm`

   Example — `MOV eax, edi` (opcode `0x89`, reg=edi=7, rm=eax=0):
   `ModRM = (3<<6)|(7<<3)|0 = 0xC0|0x38|0x00 = 0xF8`
   Full encoding: `89 F8`

   Example — `ADD eax, esi` (opcode `0x01`, reg=esi=6, rm=eax=0):
   `ModRM = (3<<6)|(6<<3)|0 = 0xC0|0x30|0x00 = 0xF0`
   Full encoding: `01 F0`

---

**The System V AMD64 ABI (calling convention)**

This is how the x86-64 Linux ABI passes function arguments and returns values:

| Argument position | Register (integer/pointer) |
|-------------------|---------------------------|
| 1st               | `rdi` (or `edi` for 32-bit int) |
| 2nd               | `rsi` (or `esi`) |
| 3rd               | `rdx` |
| 4th               | `rcx` |
| Return value      | `rax` (or `eax`) |

Callee-saved registers (`rbx`, `rbp`, `r12`–`r15`) must be preserved across
calls. For these tiny leaf functions you don't use them, so that's not a
concern.

For `int add(int a, int b)`:
- `a` arrives in `edi`, `b` arrives in `esi`.
- You compute `edi + esi`, put the result in `eax`, and `RET`.

---

**`mmap` and `mprotect` — getting executable memory**

Memory returned by `malloc` is not executable on modern Linux; the NX/XD bit
in the page table entry forbids it. To get executable memory you use `mmap`:

```c
void *code = mmap(NULL, 4096,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  -1, 0);
if (code == MAP_FAILED) { perror("mmap"); exit(1); }
```

After writing your instruction bytes into the region, switch the protection:

```c
if (mprotect(code, 4096, PROT_READ | PROT_EXEC) == -1) {
    perror("mprotect"); exit(1);
}
```

This embodies the **W^X** (Write XOR Execute) principle: a memory page should
never be simultaneously writable and executable. You write first (WRITE
permission), then flip to execute (EXEC permission, removing WRITE). This is a
security hardening requirement enforced by modern OS kernels and hardware
alike (OpenBSD mandates it strictly; Linux enforces it when SELinux or
seccomp policies are active).

The reason W^X matters: if memory can be simultaneously written and executed,
an attacker who achieves arbitrary write (e.g., via a use-after-free or heap
overflow into a JIT buffer) can write shellcode and trigger execution without
any separate "make this executable" step. Always remove WRITE before adding
EXEC.

---

**Relative branch displacements**

`JMP rel8` (opcode `0xEB`) and conditional jumps like `JLE rel8` (opcode
`0x7E`) encode a **signed 8-bit displacement relative to the start of the
next instruction** — i.e., relative to the byte immediately after the jump
instruction itself.

If the instruction encoding is `EB XX` at offset `base` in your buffer, the
CPU will jump to `base + 2 + (int8_t)XX`. To jump forward 5 bytes past the
JMP, `XX = 0x05`. To jump backward 8 bytes (including the 2-byte JMP itself),
`XX = 0xF8` (signed -8 wraps to `0xF8` in uint8_t).

The typical pattern: lay out your instruction bytes, leave a placeholder
displacement byte (`0x00`), continue writing subsequent instructions, then
**patch the displacement** once you know the target offset:

```c
size_t jmp_off = off;   // record where the JMP is
code[off++] = 0xEB;
code[off++] = 0x00;     // placeholder — will be patched
// ... write more instructions ...
// Now patch: displacement = (current off) - (jmp_off + 2)
code[jmp_off + 1] = (uint8_t)((off) - (jmp_off + 2));
```

For backward jumps (loop back to an earlier point):
```c
// target_off is the offset of the instruction we want to jump back to
code[off++] = 0xEB;
code[off++] = (uint8_t)((int8_t)(target_off - (off)));
// because after writing jmp + displacement, off points one past the displacement byte
// displacement = target_off - (off_of_displacement_byte + 1)
```

---

#### How to Approach This in C

1. **Allocate** 4096 bytes with `mmap`. Check for `MAP_FAILED`.

2. **Write bytes** using a `uint8_t *` pointer and an `off` counter:
   ```c
   size_t off = 0;
   size_t add_off = off;        // remember where add() starts
   code[off++] = 0x89;          // MOV eax, edi
   code[off++] = 0xF8;
   code[off++] = 0x01;          // ADD eax, esi
   code[off++] = 0xF0;
   code[off++] = 0xC3;          // RET
   ```

3. For `IMUL r32, r/m32` (two-byte opcode `0x0F 0xAF`):
   - `IMUL eax, esi`: opcodes `0F AF`, then ModRM with reg=eax=0, rm=esi=6:
     `ModRM = (3<<6)|(0<<3)|6 = 0xC0|0x00|0x06 = 0xC6`
   - Full encoding: `0F AF C6`

4. For the `fibonacci` stretch goal, you'll need 64-bit operations (the
   argument and return value are `long`). Prefix 64-bit `MOV` and `ADD`
   instructions with `0x48` (REX.W). Use `CMP`, a conditional jump, `XOR` for
   zeroing, a counter register, and loop back with `JMP`. Use the
   patch-displacement technique described above for forward jumps past base
   cases.

5. After writing all functions, call `mprotect` to remove WRITE and add EXEC.

6. Cast to function pointers via `void *`:
   ```c
   typedef int (*add_fn)(int, int);
   add_fn add = (add_fn)(void *)(code + add_off);
   ```

7. **Debugging**: if `./jit` crashes with SIGSEGV:
   - Run under `strace ./jit` to confirm `mprotect` succeeds.
   - Use `gdb`, set a breakpoint at the `call` instruction, and disassemble
     with `x/10i <code_pointer>` to inspect what bytes you actually emitted.
   - You can also write the bytes to a file and disassemble with
     `objdump -b binary -m i386:x86-64 -D bytes.bin`.

---

#### Key Pitfalls & Security/Correctness Gotchas

- **Forgetting REX.W for 64-bit operands**: `MOV rax, rdi` is `48 89 F8`, not
  `89 F8`. Without the `0x48` prefix the instruction operates on 32-bit
  registers. For `int` arguments this is fine (they're 32-bit), but for `long`
  (the fibonacci signature) you must use REX-prefixed variants everywhere.

- **Branch displacement is relative to the next instruction**, not the opcode
  byte. A `JLE` at offset 10 is a 2-byte instruction; the displacement is
  relative to offset 12. Off-by-one sends execution into random memory with no
  error — the CPU will just start executing whatever bytes happen to be there.

- **Missing `RET` at the end of a JIT function**: execution falls through into
  the bytes of the next function (or unmapped memory). This is exactly the
  same failure mode as a stack buffer overflow corrupting the return address.
  Always double-check that every code path through your JIT function ends with
  `0xC3`.

- **`mprotect` checks the return value**: `mprotect` can fail (e.g., if the
  page-aligned size is wrong, or a security policy denies it). If you proceed
  without checking, the page stays WRITE-only and the `call` instruction will
  generate SIGSEGV (or SIGBUS on some kernels) at the point of execution, which
  is confusing to debug.

- **`mprotect` requires page-aligned addresses**: `mmap` always returns a
  page-aligned pointer, so this isn't an issue here. If you ever try to JIT
  into a `malloc`'d buffer, you must use `posix_memalign` with `getpagesize()`
  as the alignment.

- **x86 I-cache coherency is automatic**: after writing code bytes into memory,
  x86 CPUs automatically maintain coherency between the data cache and the
  instruction cache. On ARM or RISC-V you would need `__builtin___clear_cache`
  or equivalent. Don't forget this if you ever port the idea.

---

#### Bonus Curiosity (Optional — if you finish early)

- **JIT spraying**: a class of exploits where an attacker controls the immediate
  values (constants) embedded in JIT-compiled code — e.g., JavaScript number
  literals — to spray known byte sequences throughout the JIT buffer. Because
  x86 encoding is dense, almost any byte sequence contains valid instructions
  somewhere in the middle. A control-flow hijack can then land in the "middle"
  of a constant and execute attacker-chosen bytes. Read "Interpreter
  Exploitation: Pointer Inference and JIT Spraying" by Dion Blazakis (2010),
  available as a CanSecWest paper.

- **eBPF is a kernel-mode JIT**: when you attach a BPF program with
  `bpf(BPF_PROG_LOAD, ...)`, the kernel verifies the bytecode, then
  JIT-compiles it to native code using the exact same mmap/mprotect pattern
  inside `arch/x86/net/bpf_jit_comp.c`. Reading that file is a real-world tour
  through x86-64 JIT compilation at production quality.

- **`perf` and JIT code**: `perf record` can't symbolize JIT-compiled functions
  because there's no ELF or DWARF info for them. The Linux `perf_map` interface
  allows a JIT runtime to write a special file to `/tmp/perf-<pid>.map`
  (one line per function: `<addr> <size> <name>`) and `perf` will use it for
  attribution. Worth knowing if you ever instrument a JIT-heavy workload.

---

### Research Guidance

1. *Intel® 64 and IA-32 Architectures Software Developer's Manual, Vol. 2A* —
   Chapter 2 "Instruction Format" for the ModRM/SIB byte tables; Appendix B
   for the one- and two-byte opcode maps. PDF freely available from intel.com.

2. `man 2 mmap` and `man 2 mprotect` — focus on `EINVAL` conditions and the
   alignment requirement.

3. OSDev Wiki "X86-64 Instruction Encoding" —
   `https://wiki.osdev.org/X86-64_Instruction_Encoding` — practical lookup
   tables for ModRM and REX encoding (much faster to navigate than the full SDM
   for a quick reference).

4. "A Whirlwind Tutorial on Creating Really Teensy ELF Executables for Linux"
   by Brian Raiter — hand-encoding byte sequences for real executables. A
   different context but the same encoding fundamentals.

5. "JIT Spraying and Mitigations" — Dion Blazakis, CanSecWest 2010 — shows
   how executable JIT memory becomes an attack surface, directly relevant to
   automotive ECU security.

---

### Topics Covered
x86-64 instruction encoding, ModRM byte, REX prefix, mmap, mprotect, W^X,
System V AMD64 ABI, calling conventions, JIT compilation, function pointers,
relative branch displacement, I-cache coherency

### Completion Criteria
1. The program compiles with `gcc -Wall -Wextra -o jit main.c` (or `clang`)
   with zero warnings.
2. Running `./jit` prints correct results for both required functions:
   `add(3, 4) = 7` and `mul(6, 7) = 42`.
3. The page transitions from `PROT_READ|PROT_WRITE` to `PROT_READ|PROT_EXEC`
   via `mprotect` **before** any function pointer is called (W^X respected);
   the return value of `mprotect` is checked.
4. The return value of `mmap` is checked against `MAP_FAILED`; the program
   exits with a nonzero status and an error message to `stderr` on failure.
5. The arithmetic results come entirely from the JIT-compiled machine code —
   no C-level `a + b` or `a * b` expressions produce the printed values.
6. *(Stretch)* `fib(10) = 55` is also printed correctly, using a JIT-compiled
   iterative loop with 64-bit registers.

### Difficulty Estimate
~20 min (knowing x86-64 encoding) | ~75 min (looking up encodings from scratch)

### Category
Programming Language Internals & Compilers
