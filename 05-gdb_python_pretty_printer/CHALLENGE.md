## Challenge [005] — GDB Python Pretty-Printer for a Tagged Union

### Language
Python 3 (GDB extension) + C (target program under debug). GDB must be
built with Python support (`gdb --configuration | grep -i python`).
No external libraries — only the `gdb` module embedded in your GDB session.

### Description
During a debugging session on a firmware simulator you hit a breakpoint
inside a small expression evaluator. The evaluator stores every runtime
value in a discriminated-union type, `Value`, that can hold a 64-bit
integer, a `double`, or a length-delimited string. When you type `p v`
in GDB you get:

```
$1 = {kind = VAL_INT, {as_int = 42, as_float = 2.0761268e-322,
      as_str = {ptr = 0x2a <error: Cannot access memory at address 0x2a>,
                len = 0}}}
```

Every wrong variant is still displayed, filling the screen with garbage.
Your task is to write `value_printer.py` — a GDB Python script that, once
sourced, turns every `Value` output into:

```
$1 = Value(int, 42)
$2 = Value(float, 3.14)
$3 = Value(str, "hello")
```

The starter C file to compile and debug against is provided in `main.c`.
Compile it with `gcc -g -O0 -Wall -Wextra -o main main.c`.

You must deliver **only** `value_printer.py`. Do not modify `main.c`
(beyond adding your own `inspect()` calls to test edge cases).

---

### Background & Teaching

This section is the core of the challenge. Read it fully before writing
a single line of Python.

#### Core Concepts

**GDB's embedded Python interpreter**

GDB has shipped with an embedded CPython interpreter since version 7.0
(2009). Almost every modern distro's GDB package is built with it enabled.
You can verify: `gdb --configuration | grep -i python`. Inside a GDB
session, the `gdb` module is automatically importable by any Python code
running within that session.

Scripts are loaded in two ways:
- **Manual**: `(gdb) source /path/to/script.py`
- **Auto-load**: if GDB finds `<binary>-gdb.py` in the same directory as
  your binary (or in `$datadir/auto-load/`), it loads it automatically
  when the inferior is opened. This is how system libraries ship their
  own printers (e.g., `libstdc++-v3` ships printers for `std::string`,
  `std::vector`, etc.).

**`gdb.Value` — the core abstraction**

Every C/C++ object in the inferior is represented as a `gdb.Value`.
Think of it as GDB's way of wrapping a block of inferior memory together
with its DWARF type information. Key operations:

| Expression                      | What it does                                         |
|---------------------------------|------------------------------------------------------|
| `v['field']`                    | Access a named struct/union field → returns `gdb.Value` |
| `v.type`                        | The `gdb.Type` of this value                         |
| `int(v)`                        | Extract a Python `int` from a numeric value          |
| `float(v)`                      | Extract a Python `float` from a floating-point value |
| `v.cast(gdb_type)`              | Reinterpret the raw bytes as a different type        |
| `v.dereference()`               | Follow a pointer (`*ptr` in C)                       |
| `v.string()`                    | Read a NUL-terminated C string from inferior memory  |
| `v.string(length=n)`            | Read exactly `n` bytes as a Python `str`             |

`gdb.Value` objects are **not** Python ints or floats. Forgetting to call
`int()` or `float()` and then trying to use them in an f-string or
comparison will silently do the wrong thing or raise a confusing error.

**`gdb.Type` — the type system**

`gdb.Value.type` returns a `gdb.Type`. Important attributes:

| Attribute         | Meaning                                                   |
|-------------------|-----------------------------------------------------------|
| `.name`           | Typedef name (e.g. `"Value"`) or `None` for anonymous    |
| `.tag`            | Struct/union/enum tag name (may equal `.name` for typedefs)|
| `.code`           | Type code: `gdb.TYPE_CODE_STRUCT`, `gdb.TYPE_CODE_ENUM`, etc. |
| `.fields()`       | List of `gdb.Field` objects (structs, unions, enums)      |

Each `gdb.Field` has `.name` (a string) and `.type` (a `gdb.Type`).

**The pretty-printer protocol**

A GDB pretty-printer is a Python object with at least one of these
methods — GDB calls them when it needs to display a value:

```
to_string(self) -> str | None
```
Returns a human-readable one-line summary. GDB uses this as the value's
top-level display. If you return `None`, GDB falls back to its default
display. This is the only method you need to implement for this challenge.

```
children(self) -> Iterator[(str, gdb.Value | str | int)]
```
Optional. Returns an iterable of `(name, value)` pairs for expandable
sub-components, shown as `{name = value, ...}`. Useful for containers.

```
display_hint(self) -> str
```
Optional hint: `"string"` makes GDB quote the `to_string()` result,
`"array"` formats children as array elements, `"map"` interleaves
key-value pairs. Not needed for this challenge.

**Registration and the lookup chain**

GDB maintains three layers of printer registries, searched in order:
1. *Per-objfile*: printers registered for a specific shared library or binary
2. *Per-progspace*: printers for the current program space
3. *Global*: catch-all printers for any inferior

Each registry is an ordered list of *printer objects*. Each printer
object is callable: `printer(val)` returns either a pretty-printer
instance (if it handles this type) or `None` (pass to next printer).

The idiomatic way to build and register printers is via `gdb.printing`:

```python
import gdb
import gdb.printing

def build_pp():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("my_app")
    # pp.add_printer(display_name, type_name_regex, printer_class)
    pp.add_printer("Value", r"^Value$", ValuePrinter)
    return pp

gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pp())
```

`RegexpCollectionPrettyPrinter` acts as the outer printer object: when
GDB calls `pp(val)`, it looks up `val.type.tag` (falling back to
`val.type.name`) against each registered regex and returns an instance of
the first matching class. If nothing matches, it returns `None`.

**How enum comparison works in the `gdb` module**

`val['kind']` returns a `gdb.Value` of type `ValueKind`. You cannot
compare it directly to a Python int with `==` and get a Python bool —
the result is another `gdb.Value`. The reliable pattern is:

```python
kind = int(val['kind'])
if kind == 0:   # VAL_INT
    ...
```

Alternatively you can compare the string representation:
```python
if str(val['kind']) == 'VAL_INT':
    ...
```
Both work; integer comparison is more robust across compiler versions that
may emit different DWARF enum names.

---

#### How to Approach This in Python

**1. Sketch the printer class**

```python
class ValuePrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        kind = int(self.val['kind'])
        if kind == 0:               # VAL_INT
            n = int(self.val['as_int'])
            return f"Value(int, {n})"
        elif kind == 1:             # VAL_FLOAT
            d = float(self.val['as_float'])
            return f"Value(float, {d:.6g})"
        elif kind == 2:             # VAL_STRING
            s   = self.val['as_str']
            length = int(s['len'])
            text = s['ptr'].string(length=length)
            return f'Value(str, "{text}")'
        return f"Value(UNKNOWN kind={kind})"
```

**2. Register it**

```python
import gdb
import gdb.printing

def build_pp():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("value_printers")
    pp.add_printer("Value", r"^Value$", ValuePrinter)
    return pp

gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pp())
```

**3. Load and test**

```
$ gcc -g -O0 -Wall -Wextra -o main main.c
$ gdb ./main
(gdb) source value_printer.py
(gdb) break inspect
(gdb) run
(gdb) p v          # first stop: VAL_INT
(gdb) continue
(gdb) p v          # second stop: VAL_FLOAT
(gdb) continue
(gdb) p v          # third stop: VAL_STRING
```

**4. Debug your printer when it silently fails**

If `p v` still shows the raw struct, your printer isn't matching. Run:

```
(gdb) set python print-stack full
(gdb) python print(gdb.parse_and_eval("v").type, \
                   gdb.parse_and_eval("v").type.tag, \
                   gdb.parse_and_eval("v").type.name)
```

This shows you exactly what GDB sees as the type name/tag, so you can
tune your regex.

---

#### Key Pitfalls & Security/Correctness Gotchas

- **`gdb.Value` is not a Python primitive.** `int(val['as_int'])` extracts
  a Python `int`; bare `val['as_int']` is a `gdb.Value`. Using it in an
  f-string calls `__str__` on the GDB object and produces the raw GDB
  representation, not the number — which may look correct but contains
  type information you don't want.

- **`.string()` reads until `\0` by default.** Your `as_str.ptr` field
  points to length-delimited data that is *not* guaranteed NUL-terminated.
  Always use `.string(length=int(s['len']))`. Calling `.string()` without
  a length on a non-NUL-terminated buffer will read into adjacent memory
  and either return garbage or raise `gdb.MemoryError`.

- **Your `to_string()` exceptions are swallowed silently.** If your
  printer raises an unhandled exception, GDB hides it and falls back to
  the raw display, making it very hard to notice the bug. Always develop
  with `set python print-stack full` enabled.

- **Type name vs tag varies with compiler and DWARF version.** For a
  plain `typedef struct { ... } Value;` without a tag (`struct Value`),
  `val.type.tag` may be `None` and only `val.type.name` will be `"Value"`.
  For a properly tagged struct (`typedef struct Value_ { ... } Value;`)
  the tag will be `"Value_"` and the name `"Value"`. Defensive printers
  check both. The provided `main.c` uses a plain typedef — check which
  attribute your GDB uses.

- **Double-registration on script re-source.** If you `source` the script
  twice, a second set of printers is appended to the registry. GDB will
  use the first match it finds, so re-sourcing does not break correctness,
  but it does leak memory for the session. For iterative development this
  is fine; for production scripts, guard with a sentinel flag.

---

#### Bonus Curiosity (Optional — if you finish early)

- **GDB `xmethods`**: a second Python extension point that lets you
  define or override C++ member function calls inside GDB expressions.
  You can make `(gdb) p my_vec.size()` work on a custom container without
  any change to the binary — the method is implemented entirely in Python.
  See `help(gdb.xmethod)` inside GDB.

- **`gdb.FinishBreakpoint`**: subclass this to get a Python callback when
  a *specific call frame returns*, not just when it enters. It gives you
  access to the return value. Useful for logging function output without
  recompiling. Structurally similar to a function "exit hook".

- **The LLDB Python API (`SBValue`, `SBType`, `SBDebugger`)**: LLDB on
  macOS/iOS has a nearly parallel model. The registration mechanism
  differs (type summary formatters vs pretty-printers), but the `SBValue`
  API is strikingly similar to `gdb.Value`. If you ever need to debug a
  cross-compiled ARM target on macOS, this knowledge transfers directly.

---

### Research Guidance
- GDB manual §23.3 "Extending GDB using Python" — specifically the
  sub-sections: *Values From Inferior*, *Types In Python*,
  *Writing a Pretty-Printer*, and *Registering a Pretty-Printer*.
- `(gdb) python help(gdb.Value)` — inspect the live API inside your
  GDB session; the built-in help is terse but accurate.
- `/usr/share/gdb/python/gdb/printing.py` — read the full implementation
  of `RegexpCollectionPrettyPrinter` (~60 lines). Understanding what
  `add_printer` and `__call__` actually do eliminates mystery.
- GDB manual §23.3.2.5 "Pretty Printing API" — formal spec for
  `to_string`, `children`, and `display_hint`.
- CPython docs on generator functions (`yield`) — `children()` is
  conventionally written as a generator; understanding how lazy iteration
  interacts with GDB's printing loop prevents subtle ordering bugs.

### Topics Covered
GDB Python API, pretty-printers, gdb.Value, gdb.Type, discriminated unions,
tagged unions, inferior memory access, Python generators, GDB auto-loading,
debugger extension scripting

### Completion Criteria
1. `gcc -g -O0 -Wall -Wextra -o main main.c` compiles with zero warnings.
2. `source value_printer.py` in GDB produces no errors or tracebacks
   (verified with `set python print-stack full` active).
3. Breaking at `inspect` for the `VAL_INT` call and running `p v` prints
   exactly `Value(int, 42)`.
4. Breaking at `inspect` for the `VAL_FLOAT` call and running `p v` prints
   `Value(float, 3.14)` (value matches to at least 3 significant figures).
5. Breaking at `inspect` for the `VAL_STRING` call and running `p v` prints
   `Value(str, "hello")`.
6. Manually calling `inspect((Value){99, {.as_int = 0}})` from GDB
   (`(gdb) call inspect((Value){99, {0}})`) and printing the argument
   does **not** raise an unhandled exception — the printer outputs a
   graceful fallback such as `Value(UNKNOWN kind=99)`.

### Difficulty Estimate
~15 min (if you have written GDB Python scripts before) | ~60 min (researching from scratch)

### Category
Build Systems & Tooling
