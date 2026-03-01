# 🔴 Mar Language

> A compiled programming language with **C-level performance** and **Python-like simplicity**.  
> No semicolons. Curly braces. Compiles to native binaries via C.

---

## What is Mar?

Mar is a compiled, statically-typed programming language written in C.  
You write clean, readable code in `.mar` files — the Mar compiler translates it into C,  
which then compiles to a native binary using your system's C compiler (gcc or clang).

**The result:** Fast native programs with a syntax that doesn't get in your way.

```
Your Code (.mar)  →  Mar Compiler  →  C Code (.c)  →  gcc/clang  →  Native Binary
```

---

## Why Mar?

| Problem with C | How Mar solves it |
|---|---|
| Semicolons everywhere | No semicolons needed |
| Verbose for loops | `for i in range(0, 10)` |
| `printf` / `scanf` complexity | Simple `print()` and `take()` |
| Switch needs colons + break | Clean switch, auto-break inserted |
| Pointer complexity for I/O | `take("%d", &x)` just works |

---

## Quick Example

```mar
int prime(int n)
{
    if(n < 2)
        return 0
    int i = 2
    while(i * i <= n)
    {
        if(n % i == 0)
            return 0
        i = i + 1
    }
    return 1
}

int main()
{
    print("Primes from 2 to 20:\n")
    for i in range(2, 21)
    {
        if(prime(i))
            print("%d ", i)
    }
    print("\n")
    return 0
}
```

**Output:**
```
Primes from 2 to 20:
2 3 5 7 11 13 17 19
```

---

## Language Features

| Feature | Example |
|---|---|
| Integer variables | `int x = 5` |
| Boolean literals | `bool flag = true` |
| Functions | `int add(int a, int b) { return a + b }` |
| If / else | `if(x > 0) { ... } else { ... }` |
| While loop | `while(x < 10) { x = x + 1 }` |
| For range loop | `for i in range(0, 10) { print("%d\n", i) }` |
| Switch / case | `switch(n) { case 1 print("one") break }` |
| Arrays | `int arr[5] = {1, 2, 3, 4, 5}` |
| Print | `print("Hello %d\n", x)` |
| Input | `take("%d", &x)` |
| Break | `break` |
| Return | `return 0` |

---

## Installation

### Requirements

| Tool | Purpose | Install |
|---|---|---|
| `gcc` or `clang` | Compiles generated C code | Comes with Xcode / Linux build tools |
| `make` | Builds the Mar compiler itself | Comes with Xcode / build-essential |
| `git` | Clone this repo | brew install git / apt install git |

---

### Mac (M1 / Intel)

```bash
# Step 1 — Install Xcode Command Line Tools (includes clang + make)
xcode-select --install

# Step 2 — Install Homebrew (Mac package manager)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Step 3 — Add Homebrew to your PATH (M1 only — run both lines)
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"

# Step 4 — Install git
brew install git

# Step 5 — Clone and build Mar
git clone https://github.com/YOUR_USERNAME/mar-lang.git
cd mar-lang
make

# Step 6 — Verify it works
./bin/mar --help
```

---

### Linux (Ubuntu / Debian)

```bash
# Step 1 — Install build tools
sudo apt update
sudo apt install gcc make git

# Step 2 — Clone and build Mar
git clone https://github.com/YOUR_USERNAME/mar-lang.git
cd mar-lang
make

# Step 3 — Verify
./bin/mar --help
```

---

### Install System-Wide (Optional)

After building, run this so `mar` works from any folder:

```bash
sudo cp bin/mar /usr/local/bin/mar
```

Then you can use `mar` anywhere:

```bash
mar myprogram.mar -o myprogram.c
```

---

## Project Structure

```
mar-lang/
│
├── src/                    ← All compiler source code (C files)
│   ├── main.c              ← Entry point — reads args, runs pipeline
│   ├── arena.c             ← Memory allocator (region-based, no leaks)
│   ├── error.c             ← Error collection and pretty printing
│   ├── ast.c               ← AST node constructors and printer
│   ├── lexer.c             ← Tokenizer — turns source text into tokens
│   ├── parser.c            ← Parser — turns tokens into AST tree
│   └── codegen_c.c         ← Code generator — turns AST into C code
│
├── include/mar/            ← Header files (public interfaces)
│   ├── arena.h             ← Arena allocator interface
│   ├── error.h             ← Error types and reporting interface
│   ├── ast.h               ← All AST node types and structures
│   ├── lexer.h             ← Lexer and Token types
│   ├── parser.h            ← Parser interface
│   └── codegen_c.h         ← C code generator interface
│
├── examples/               ← Sample .mar programs
│   ├── hello.mar           ← Hello World
│   ├── switch_test.mar     ← Switch / case demo
│   └── primes.mar          ← Prime numbers (shows all features)
│
├── tests/                  ← Test suite
│   ├── integration/        ← Full .mar programs that should compile+run
│   └── integration/expected/ ← Expected output files for each test
│
├── docs/                   ← Language documentation
├── bin/                    ← Compiled binary goes here (after make)
├── build/                  ← Object files go here (after make)
└── Makefile                ← Build instructions
```

---

## How the Compiler Works

The Mar compiler is a classic multi-pass compiler. Every `.mar` file goes through these stages in order:

```
┌─────────────────────────────────────────────────────────────────┐
│                     MAR COMPILER PIPELINE                       │
└─────────────────────────────────────────────────────────────────┘

  your_program.mar
         │
         ▼
  ┌────────────┐
  │   LEXER    │  src/lexer.c
  │            │  Reads source text character by character.
  │            │  Produces a stream of Tokens.
  │            │  Example: "int x = 5" → [INT][IDENT:x][ASSIGN][INT_LIT:5]
  └─────┬──────┘
        │  Token[]
        ▼
  ┌────────────┐
  │   PARSER   │  src/parser.c
  │            │  Reads tokens. Builds an AST (Abstract Syntax Tree).
  │            │  Uses recursive descent parsing.
  │            │  Example: sees INT IDENT ASSIGN → creates VarDecl node
  └─────┬──────┘
        │  Program* (tree of nodes)
        ▼
  ┌──────────────┐
  │  CODE GEN    │  src/codegen_c.c
  │              │  Walks the AST. Writes equivalent C code.
  │              │  for-range → while loop
  │              │  print()  → printf()
  │              │  take()   → scanf()
  │              │  switch without colons → valid C switch
  └─────┬────────┘
        │  output.c
        ▼
  ┌──────────────┐
  │  gcc / clang │  (your system's C compiler — not part of Mar)
  └─────┬────────┘
        │
        ▼
   native binary  ✓
```

**Cross-cutting components used by every stage:**

- `arena.c` — All memory is allocated in a single region (arena). One call frees everything. Zero memory leaks by design.
- `error.c` — Errors from any stage are collected and printed together with the source line and a `^` caret pointing to the exact column.

---

## Usage

### Compile a .mar file

```bash
./bin/mar yourprogram.mar -o yourprogram.c
```

This produces `yourprogram.c` — a valid C file you can inspect.

### Compile the generated C to a binary

```bash
cc -o yourprogram yourprogram.c
```

### Run your program

```bash
./yourprogram
```

### All three steps in one line

```bash
./bin/mar yourprogram.mar -o /tmp/out.c && cc -o /tmp/out /tmp/out.c && /tmp/out
```

---

## Compiler Flags

```
Usage: mar <file.mar> [options]

Options:
  -o <file>        Output file name          (default: a.out.c)
  --dump-tokens    Print all tokens and exit (debug: see what the lexer sees)
  --dump-ast       Print AST summary and exit (debug: see what the parser built)
  --help           Show this help message
```

### Debug examples

```bash
# See every token the lexer produces
./bin/mar examples/primes.mar --dump-tokens

# See the AST (function names and structure)
./bin/mar examples/primes.mar --dump-ast

# See the generated C code
./bin/mar examples/primes.mar -o /tmp/out.c && cat /tmp/out.c
```

---

## Language Reference

### Types

```mar
int x           ← integer (32-bit)
float y         ← double-precision float
char c          ← single character
bool flag       ← true or false
int arr[5]      ← array of 5 integers
```

### Variables

```mar
int x           ← declare (value is 0)
int x = 10      ← declare and initialize
int arr[3] = {1, 2, 3}   ← array with initializer
```

### Operators

```mar
+   -   *   /   %        ← arithmetic
==  !=  <   >   <=  >=   ← comparison
&&  ||  !                ← logical
=   +=  -=  *=  /=  %=   ← assignment
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
```

Single-line (no braces needed):
```mar
if(x > 0)
    print("positive\n")
else
    print("not positive\n")
```

### While Loop

```mar
int i = 0
while(i < 10)
{
    print("%d\n", i)
    i = i + 1
}
```

### For Range Loop

```mar
for i in range(0, 5)
{
    print("%d\n", i)
}
```

This is equivalent to `for (int i = 0; i < 5; i++)` in C.  
The start is **inclusive**, the end is **exclusive**.

```mar
for i in range(1, 6)    ← prints 1 2 3 4 5
for i in range(0, n)    ← variables work too
for i in range(a, b+1)  ← expressions work too
```

### Switch / Case

```mar
switch(n)
{
    case 1
        print("One\n")
        break
    case 2
        print("Two\n")
        break
    default
        print("Other\n")
}
```

Note: No colons after `case` or `default`. Break is optional — if you omit it,  
the compiler inserts one automatically to prevent accidental fallthrough.

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

Functions can call each other in any order — no forward declarations needed.

### Print

```mar
print("Hello World\n")
print("x = %d\n", x)
print("sum = %d, product = %d\n", a+b, a*b)
```

Uses the same format specifiers as C's `printf`:
- `%d` → integer
- `%f` → float
- `%c` → character
- `%s` → string
- `\n` → newline

### Take (Input)

```mar
int x
take("%d", &x)

int a, b
take("%d %d", &a, &b)
```

Uses the same format specifiers as C's `scanf`.  
Always use `&` before the variable name.

### Arrays

```mar
int arr[5]                    ← declare array of 5 ints
int arr[5] = {1, 2, 3, 4, 5} ← declare with initializer
arr[0] = 10                   ← set element
print("%d\n", arr[2])         ← read element
```

### Boolean

```mar
bool flag = true
bool done = false

if(flag)
    print("yes\n")

while(!done)
{
    print("looping\n")
    done = true
}
```

---

## Error Messages

Mar gives you clear errors with the exact line, column, and a caret (`^`) pointing to the problem.

**Example — undefined variable:**
```
SemanticError at primes.mar:7:8
  Undefined variable 'primme'

     7 |     if(primme(i))
         |        ^
```

**Example — unexpected token:**
```
ParserError at hello.mar:3:5
  Expected '}' but got 'return'

     3 |     return 0
         |     ^
```

**Error types:**
| Type | What it means |
|---|---|
| `LexerError` | Invalid character in source file |
| `ParserError` | Code structure is wrong (missing brace, etc.) |
| `SemanticError` | Logic error (undefined variable, break outside loop, etc.) |
| `CodegenError` | Internal compiler error during C output |

---

## Full Example Programs

### Hello World

```mar
int main()
{
    print("Hello, World!\n")
    return 0
}
```

### Switch Statement

```mar
int main()
{
    int n = 2
    switch(n)
    {
        case 1
            print("One\n")
            break
        case 2
            print("Two\n")
            break
        default
            print("Other\n")
    }
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

### User Input

```mar
int main()
{
    int n
    print("Enter a number: ")
    take("%d", &n)
    print("You entered: %d\n", n)
    return 0
}
```

### Array Sum

```mar
int main()
{
    int arr[5] = {10, 20, 30, 40, 50}
    int sum = 0
    for i in range(0, 5)
    {
        sum = sum + arr[i]
    }
    print("Sum = %d\n", sum)
    return 0
}
```

---

## Building from Source

```bash
# Clone the repository
git clone https://github.com/YOUR_USERNAME/mar-lang.git
cd mar-lang

# Build (produces bin/mar)
make

# Build optimized release version
make release

# Remove all build files and start fresh
make clean

# Build and run all tests
make test
```

---

## How to Add a New Feature (for contributors)

The compiler has a clean separation of stages. Here is where each feature lives:

| What to change | Where |
|---|---|
| Add a new keyword | `src/lexer.c` → `KEYWORDS[]` array |
| Add new syntax | `src/parser.c` → add a case in `parse_stmt()` |
| Add a new AST node | `include/mar/ast.h` → add to `StmtKind` or `ExprKind` enum + union |
| Change how something compiles to C | `src/codegen_c.c` → add a case in `emit_stmt()` or `emit_expr()` |
| Add a new type | `include/mar/ast.h` → add to `TypeKind` enum |
| Change error messages | `src/error.c` |

**Order to read the source code if you're learning:**
1. `include/mar/ast.h` — understand all the data structures first
2. `src/lexer.c` — see how text becomes tokens
3. `src/parser.c` — see how tokens become a tree
4. `src/codegen_c.c` — see how the tree becomes C code
5. `src/main.c` — see how it all connects

---

## Roadmap

- [x] int type
- [x] float type
- [x] char type
- [x] bool type with true/false
- [x] Functions with parameters and return values
- [x] if / else (with and without braces)
- [x] while loop
- [x] for i in range(a, b)
- [x] switch / case / default (no colons)
- [x] break / return
- [x] print() → printf()
- [x] take() → scanf()
- [x] Arrays with initializer syntax
- [x] Compound assignment operators (+=, -=, etc.)
- [ ] String type
- [ ] Nested functions / closures
- [ ] Structs / records
- [ ] Multiple source files / import system
- [ ] Standard library
- [ ] LLVM IR backend (for better optimization)
- [ ] LSP (language server for editor support)

---

## Technical Design Decisions

**Why compile to C instead of assembly?**  
C gives us portability (works on Mac M1, Linux x86, Windows) and we get GCC/Clang's optimizers for free. The generated C is readable and debuggable.

**Why use an arena allocator?**  
All AST nodes are allocated in a single memory region. At the end of compilation, one call frees everything. This makes the compiler fast and eliminates memory leaks by design — no per-node `free()` needed anywhere.

**Why no semicolons?**  
Semicolons are noise. The parser uses newlines and the structure of statements to know where each statement ends. This makes code cleaner and reduces syntax errors.

**Why auto-insert break in switch?**  
C's switch fallthrough is one of the most common sources of bugs. Mar inserts `break` automatically after each case unless you already wrote one, preventing accidental fallthrough while still letting you use `break` explicitly.

---

## License

MIT License — free to use, modify, and distribute.

---

## Author

Built by **Aditya Rao**  
Language: C | Target: C / native binary | Platform: Mac, Linux

---

## Getting Help

If something is not working:

1. Make sure you are in the `mar-lang` directory: `cd ~/mar-lang`
2. Make sure the compiler is built: `ls bin/mar`
3. If `bin/mar` is missing, run: `make clean && make`
4. If `make` fails, check that Xcode tools are installed: `xcode-select --install`
5. Open an issue on GitHub with the exact error message