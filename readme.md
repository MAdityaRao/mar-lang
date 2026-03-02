# Mar Language

> A compiled, statically-typed language with C-level performance and a clean, minimal syntax.  
> Compiles to native binaries via C. No semicolons. No header files. No friction.

---

## What is Mar?

Mar is a compiled language written in C. You write `.mar` source files, and one command compiles and runs your program. Behind the scenes Mar translates your code to C and invokes `gcc` or `clang` automatically.

```
source.mar  →  Mar compiler  →  output.c  →  gcc/clang  →  native binary
```

The generated C is readable, portable, and benefits from the full GCC/Clang optimization pipeline.

---

## Why Mar?

| Pain point in C | How Mar addresses it |
|---|---|
| Semicolons on every line | No semicolons |
| Verbose `for` loops | `for i in range(0, 10)` |
| `printf` / `scanf` boilerplate | `print()` and `take()` |
| Switch fallthrough bugs | `break` auto-inserted per case |
| Struct + manual init boilerplate | `class` with `init()` and `new` |
| Manual heap management | Arena-allocated objects, zero leaks |
| `->` vs `.` confusion | Always dot syntax — compiler handles the rest |
| Compile, link, run as three steps | `mar run program.mar` does everything |

---

## Quick Start

```bash
git clone https://github.com/adityarao/mar-lang.git
cd mar-lang
make
./bin/mar run examples/primes.mar
```

---

## Installation

### Requirements

| Tool | Purpose |
|---|---|
| `gcc` or `clang` | Compiles the generated C output |
| `make` | Builds the Mar compiler itself |
| `git` | Clones this repository |

### macOS (M1 / Intel)

```bash
# Install Xcode command line tools (includes clang and make)
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# M1 only — add Homebrew to PATH
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"

# Clone and build
git clone https://github.com/adityarao/mar-lang.git
cd mar-lang
make

./bin/mar --help
```

### Linux (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install gcc make git

git clone https://github.com/adityarao/mar-lang.git
cd mar-lang
make

./bin/mar --help
```

### Install system-wide (optional)

```bash
sudo cp bin/mar /usr/local/bin/mar
```

After this, `mar` works from any directory.

---

## Usage

### Compile and run

```bash
mar run yourprogram.mar
```

### Compile to C only (for inspection)

```bash
./bin/mar yourprogram.mar -o yourprogram.c
```

### Debug flags

```bash
./bin/mar yourprogram.mar --dump-tokens   # print every lexer token
./bin/mar yourprogram.mar --dump-ast      # print the parsed AST summary
```

### Start the language server

```bash
mar lsp
```

Connects over stdin/stdout using the LSP JSON-RPC protocol. Supports hover documentation, diagnostics on save, and keyword completion.

---

## Language Reference

### Types

| Mar type | C equivalent | Notes |
|---|---|---|
| `int` | `int64_t` | 64-bit signed integer |
| `float` | `double` | 64-bit IEEE 754 |
| `char` | `char` | Single byte |
| `bool` | `bool` | `true` or `false` |
| `string` | `const char*` | Immutable UTF-8 string |
| `int[N]` | `int64_t arr[N]` | Fixed-size array |
| `ClassName` | `ClassName*` | Class instance (heap pointer) |

### Variables

```mar
int x               // zero-initialised
int x = 10          // declare and assign
int arr[3] = {1, 2, 3}
string name = "Mar"
Player p = new Player(100)
```

### Operators

```mar
+   -   *   /   %         // arithmetic
==  !=  <   >   <=  >=    // comparison
&&  ||  !                 // logical
=   +=  -=  *=  /=  %=    // assignment
.                         // member access
&                         // address-of (for take())
```

### If / Else

```mar
if(x > 0)
{
    print("positive\n")
}
else
{
    print("not positive\n")
}

// Single-line form (braces optional)
if(x > 0)
    print("positive\n")
```

### While

```mar
int i = 0
while(i < 10)
{
    print("%d\n", i)
    i = i + 1
}
```

### For range

```mar
for i in range(0, 5)
{
    print("%d\n", i)
}
```

Start is inclusive, end is exclusive. Equivalent to `for (int i = 0; i < 5; i++)` in C.

### For array iteration

```mar
int scores[3] = {10, 20, 30}
for s in scores
{
    print("%d\n", s)
}
```

### Switch

```mar
switch(n)
{
    case 1
        print("one\n")
        break
    case 2
        print("two\n")
        break
    default
        print("other\n")
}
```

No colons after `case` or `default`. If you omit `break`, the compiler inserts one automatically.

### Functions

```mar
int add(int a, int b)
{
    return a + b
}

int main()
{
    int result = add(3, 4)
    print("Result: %d\n", result)
    return 0
}
```

Functions can be called before they are defined — no forward declarations needed.

### Multiple return values

```mar
int, int swap(int a, int b)
{
    return b, a
}

int main()
{
    int x, int y = swap(1, 2)
    print("%d %d\n", x, y)
    return 0
}
```

### Strings

```mar
string greeting = "Hello"
string name = "World"
string message = greeting + ", " + name + "!\n"
print(message)

int n = len(greeting)    // 5
```

String concatenation via `+` allocates from the runtime arena.

### Arrays

```mar
int arr[5] = {10, 20, 30, 40, 50}
arr[0] = 99
print("%d\n", arr[2])         // 30
print("len = %d\n", len(arr)) // 5
```

### Print and take

```mar
print("x = %d\n", x)
print("name = %s\n", name)

int n
take("%d", &n)

int a, b
take("%d %d", &a, &b)
```

`print` wraps `printf`. `take` wraps `scanf`. Same format specifiers apply.

### Classes

```mar
class Player
{
    int health
    int score

    void init(int h)
    {
        health = h
        score = 0
    }

    void damage(int amount)
    {
        health = health - amount
    }

    int is_alive()
    {
        if(health > 0)
            return 1
        return 0
    }
}

int main()
{
    Player p = new Player(100)
    p.damage(20)
    print("HP: %d alive: %d\n", p.health, p.is_alive())
    return 0
}
```

- `new ClassName(args)` allocates from the arena and calls `init(args)`.
- Methods reference fields directly — no `this->` syntax needed.
- Instances are always pointers. `p.health` compiles to `p->health` in C.
- Always define an `init` method; `new` always calls it.

### Inheritance

```mar
class Animal
{
    string name

    void init(string n) { name = n }
    void speak() { print("...\n") }
}

class Dog extends Animal
{
    void speak() { print("%s says woof\n", name) }
}

int main()
{
    Dog d = new Dog("Rex")
    d.speak()
    return 0
}
```

Child classes inherit all parent fields. Methods can be overridden. Non-overridden parent methods are forwarded automatically.

### Null

```mar
Player p = null

if(p == null)
    print("no player\n")
```

### Import

```mar
import "utils.mar"       // user file, resolved relative to current directory

import "mar/math"        // standard: sqrt, pow, floor, ceil, min, max, abs
import "mar/str"         // standard: mar_str_len, mar_str_cmp, mar_str_to_int, mar_int_to_str
import "mar/io"          // standard: mar_file_read, mar_file_write
```

---

## How the Compiler Works

```
source.mar
    │
    ▼
┌──────────┐
│  Lexer   │  src/lexer.c
│          │  Converts source text to a flat token array.
│          │  "int x = 5" → [INT] [IDENT:x] [ASSIGN] [INT_LIT:5]
└────┬─────┘
     │ Token[]
     ▼
┌──────────┐
│  Parser  │  src/parser.c
│          │  Recursive descent. Builds an AST from the token stream.
│          │  Handles operator precedence, class declarations, imports.
└────┬─────┘
     │ Program*
     ▼
┌──────────┐
│ Codegen  │  src/codegen_c.c
│          │  Walks the AST and writes C source.
│          │  for-range  → while loop
│          │  class      → typedef struct + prefixed functions
│          │  new Foo()  → arena_alloc + Foo_init()
│          │  p.field    → p->field
│          │  print()    → printf()
└────┬─────┘
     │ output.c
     ▼
gcc / clang
     │
     ▼
native binary
```

**Cross-cutting components:**

- `arena.c` — All compiler memory lives in a single region. One call frees everything at exit. Mar program objects created with `new` are also arena-allocated at runtime.
- `error.c` — Errors from any stage are collected and reported together with the source line and a `^` caret pointing to the exact column.
- `lsp.c` — JSON-RPC language server (hover, diagnostics, completion) served over stdin/stdout.

---

## Project Structure

```
mar-lang/
├── src/
│   ├── main.c          entry point, argument parsing, pipeline orchestration
│   ├── arena.c         region allocator
│   ├── error.c         error collection and formatted output
│   ├── ast.c           AST node constructors and debug printer
│   ├── lexer.c         tokeniser
│   ├── parser.c        recursive descent parser
│   ├── codegen_c.c     C code generator
│   └── lsp.c           LSP language server
├── include/mar/
│   ├── arena.h
│   ├── error.h
│   ├── ast.h           all node types, enums, and structs
│   ├── lexer.h
│   ├── parser.h
│   ├── codegen_c.h
│   └── lsp.h
├── examples/           sample .mar programs
├── tests/
│   ├── integration/           .mar programs used as test inputs
│   └── integration/expected/  expected stdout for each test
├── docs/
├── bin/                compiled binary (after make)
├── build/              object files (after make)
├── Makefile
└── install.sh
```

---

## Build Targets

```bash
make              # debug build  (bin/mar)
make release      # optimised build with -O2
make debug        # build with AddressSanitizer and UBSan
make test         # compile and run all integration tests
make examples     # run every .mar file in examples/
make clean        # remove bin/ and build/
```

---

## Example Programs

### Hello World

```mar
int main()
{
    print("Hello, World!\n")
    return 0
}
```

### FizzBuzz

```mar
int main()
{
    for i in range(1, 101)
    {
        if(i % 15 == 0)
            print("FizzBuzz\n")
        else if(i % 3 == 0)
            print("Fizz\n")
        else if(i % 5 == 0)
            print("Buzz\n")
        else
            print("%d\n", i)
    }
    return 0
}
```

### Primes

```mar
int is_prime(int n)
{
    if(n < 2) return 0
    int i = 2
    while(i * i <= n)
    {
        if(n % i == 0) return 0
        i = i + 1
    }
    return 1
}

int main()
{
    print("Primes up to 50: ")
    for i in range(2, 51)
    {
        if(is_prime(i))
            print("%d ", i)
    }
    print("\n")
    return 0
}
```

```
Primes up to 50: 2 3 5 7 11 13 17 19 23 29 31 37 41 43 47
```

### RPG Player (classes)

```mar
class Player
{
    int health
    int score
    int kills

    void init(int h)
    {
        health = h
        score = 0
        kills = 0
    }

    void fight(int enemy_level)
    {
        health = health - enemy_level
        score  = score + enemy_level
        kills  = kills + 1
    }
}

int main()
{
    Player hero = new Player(100)

    for i in range(1, 6)
    {
        hero.fight(i)
        print("Fight %d: HP=%d Score=%d\n", i, hero.health, hero.score)
    }

    print("Total kills: %d\n", hero.kills)
    return 0
}
```

```
Fight 1: HP=99 Score=1
Fight 2: HP=97 Score=3
Fight 3: HP=94 Score=6
Fight 4: HP=90 Score=10
Fight 5: HP=85 Score=15
Total kills: 5
```

---

## Error Messages

Mar reports errors with the file, line, column, and a caret pointing to the problem.

```
ParserError at hello.mar:3:5
  Expected '}' but got 'return'

     3 |     return 0
           |     ^
```

| Error type | Meaning |
|---|---|
| `LexerError` | Unrecognised character in source |
| `ParserError` | Structural problem — missing brace, unexpected token, etc. |
| `SemanticError` | Logic problem — undefined variable, type mismatch, etc. |
| `CodegenError` | Internal error during C output |

---

## Contributing

The compiler has a clean stage separation. Each feature maps directly to one or two files.

| Change | Where |
|---|---|
| New keyword | `src/lexer.c` → `KEYWORDS[]` |
| New single-character token | `src/lexer.c` → single-char `switch` in `lexer_tokenize` |
| New syntax / statement | `src/parser.c` → `parse_stmt()` |
| New AST node | `include/mar/ast.h` → `StmtKind` or `ExprKind` + union field |
| New code generation rule | `src/codegen_c.c` → `emit_stmt()` or `emit_expr()` |
| New type | `include/mar/ast.h` → `TypeKind` |
| Class features | `src/parser.c` → `parse_class()`, `src/codegen_c.c` → `emit_class()` |
| Error messages | `src/error.c` |

**Recommended reading order for new contributors:**
1. `include/mar/ast.h` — all data structures
2. `src/lexer.c` — text to tokens
3. `src/parser.c` — tokens to AST
4. `src/codegen_c.c` — AST to C
5. `src/main.c` — pipeline wiring

---

## Design Notes

**Why compile to C instead of machine code directly?**  
C gives portability across Mac M1, Linux x86, and Windows, and the generated output is readable and debuggable. GCC and Clang's optimisers handle the heavy lifting for free.

**Why an arena allocator?**  
All AST nodes live in a single memory region. At the end of compilation one call frees everything — no per-node `free()`, no leaks by design. Mar program objects created with `new` use the same strategy at runtime.

**Why no semicolons?**  
The parser infers statement boundaries from context. Semicolons are optional noise that Mar omits by design.

**Why auto-insert `break` in switch?**  
C's implicit fallthrough is one of the most common sources of correctness bugs. Mar inserts `break` automatically after each case unless one is already present.

**Why does `new` use an arena instead of `malloc`?**  
Objects live for the program's lifetime. Arena allocation means no garbage collector, no `free()` calls at exit, and no use-after-free issues.

**Why does `p.field` compile to `p->field`?**  
All Mar class instances are pointers. The dot operator always means pointer dereference under the hood — the compiler handles the translation transparently.

---

## Roadmap

- [x] int, float, char, bool types
- [x] string type with `+` concatenation and `len()`
- [x] null literal
- [x] Arrays with static initializers and `len()`
- [x] For-in-array loop
- [x] Functions with parameters and return values
- [x] Multiple return values
- [x] if / else
- [x] while loop
- [x] for i in range(a, b)
- [x] switch / case / default (no colons, auto-break)
- [x] Compound assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`)
- [x] print() and take()
- [x] Classes with fields and methods
- [x] new keyword and arena-allocated objects
- [x] Dot operator for field access and method calls
- [x] Inheritance with extends
- [x] import (user files and mar/math, mar/str, mar/io)
- [x] LSP language server (hover, diagnostics, completion)
- [ ] Closures / first-class functions
- [ ] Generics
- [ ] LLVM IR backend

---

## Troubleshooting

1. Confirm you are in the project root: `cd ~/mar-lang`
2. Confirm the binary exists: `ls bin/mar`
3. If missing, rebuild: `make clean && make`
4. On macOS, if `make` fails: `xcode-select --install`
5. Test with a known-good file: `./bin/mar run examples/hello.mar`
6. Open an issue on GitHub with the full error output

---

## License

MIT — free to use, modify, and distribute.

---

## Author

**Aditya Rao**  
Language: C | Target: C → native binary | Platforms: macOS, Linux