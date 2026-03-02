
```markdown
# Mar Language

> A compiled, statically-typed language with C-level performance and a clean, minimal-friction syntax.  
> Compiles to native binaries via C. No semicolons. No header files. No format strings. No memory leaks.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Philosophy & Design](#2-philosophy--design)
3. [Installation Guide](#3-installation-guide)
    - [macOS (Apple Silicon & Intel)](#macos-apple-silicon--intel)
    - [Linux (Ubuntu / Debian)](#linux-ubuntu--debian)
    - [Windows (WSL)](#windows-wsl)
4. [Compiler CLI Usage](#4-compiler-cli-usage)
    - [Run Mode (Scripting)](#run-mode-scripting)
    - [Build Mode (Native Binaries)](#build-mode-native-binaries)
    - [Tooling & LSP](#tooling--lsp)
5. [Language Reference](#5-language-reference)
    - [Primitive Types](#primitive-types)
    - [Variables & Declarations](#variables--declarations)
    - [Type Casting](#type-casting)
    - [Operators & Expressions](#operators--expressions)
    - [Control Flow](#control-flow)
    - [Functions & Multi-Return](#functions--multi-return)
    - [Arrays & Iteration](#arrays--iteration)
    - [Strings](#strings)
    - [Frictionless I/O](#frictionless-io)
    - [Object-Oriented Programming](#object-oriented-programming)
    - [Null Handling](#null-handling)
6. [Standard Library (Stdlib)](#6-standard-library-stdlib)
    - [mar/math](#marmath)
    - [mar/str](#marstr)
    - [mar/io](#mario)
7. [Compiler Architecture (Under the Hood)](#7-compiler-architecture-under-the-hood)
    - [The Pipeline](#the-pipeline)
    - [The Arena Allocator](#the-arena-allocator)
    - [C11 _Generic & Type Inference](#c11-_generic--type-inference)
8. [Example Programs](#8-example-programs)
9. [Error Handling & Diagnostics](#9-error-handling--diagnostics)
10. [Contributing Guide](#10-contributing-guide)
11. [Roadmap](#11-roadmap)
12. [License & Author](#12-license--author)

---

## 1. Introduction

Mar is a modern programming language designed for developers who want the raw speed and zero-dependency deployment of C, without enduring C's archaic syntax, unsafe memory pitfalls, and tedious boilerplate.

Mar is technically a **compiler frontend**. You write `.mar` source files, and the Mar compiler parses them, constructs a strict Abstract Syntax Tree (AST), and lowers that AST into highly optimized, safe, standard C code. It then automatically orchestrates your system's native C compiler (`gcc` or `clang`) to produce a final, standalone native executable.

```text
source.mar  →  Mar compiler  →  Safe C11 Code  →  GCC/Clang  →  Native Binary

```

---

## 2. Philosophy & Design

Mar eliminates the friction of systems programming. We addressed the most painful aspects of C/C++ and replaced them with modern, ergonomic solutions.

| Pain point in C / C++ | The Mar Solution |
| --- | --- |
| **Semicolons on every line** | **Zero semicolons.** The parser dynamically infers statement boundaries. |
| **`printf` / `scanf` formatting** | **Format-free I/O.** `print(a)` and `take(a)` use C11 generic macros to auto-infer types. |
| **Verbose `for` loops** | **Pythonic iteration.** `for i in range(0, 10)` and `for item in arr`. |
| **Switch fallthrough bugs** | **Safe Switches.** `break` is auto-inserted per case. Fallthrough is impossible. |
| **Manual memory management** | **Arena-allocated objects.** Zero `malloc`/`free` calls. Zero memory leaks by design. |
| **`->` vs `.` pointer confusion** | **Unified access.** Always use dot syntax (`obj.field`). The compiler translates pointers. |
| **Struct + manual init** | **Native OOP.** `class` definitions with mandatory `init()` methods and `new` instantiation. |
| **Cluttered workspaces** | **Auto-cleanup.** Mar instantly cleans up intermediate `.c` files and binaries. |

---

## 3. Installation Guide

Mar requires a working C compiler (`gcc` or `clang`) and `make` to build the compiler from source.

### macOS (Apple Silicon & Intel)

macOS ships with Clang via the Xcode Command Line Tools.

```bash
# 1. Install Xcode command line tools
xcode-select --install

# 2. Clone the repository
git clone [https://github.com/adityarao/mar-lang.git](https://github.com/adityarao/mar-lang.git)
cd mar-lang

# 3. Build the Mar compiler
make

# 4. Install system-wide (allows running 'mar' from anywhere)
sudo cp bin/mar /usr/local/bin/mar

```

### Linux (Ubuntu / Debian)

```bash
# 1. Install prerequisites
sudo apt update
sudo apt install gcc make git

# 2. Clone the repository
git clone [https://github.com/adityarao/mar-lang.git](https://github.com/adityarao/mar-lang.git)
cd mar-lang

# 3. Build the compiler
make

# 4. Install system-wide
sudo cp bin/mar /usr/local/bin/mar

```

### Windows (WSL)

Mar is built for POSIX-compliant systems. Windows users should use the Windows Subsystem for Linux (WSL).

```bash
# Inside your WSL Ubuntu terminal:
sudo apt update && sudo apt install build-essential git
git clone [https://github.com/adityarao/mar-lang.git](https://github.com/adityarao/mar-lang.git)
cd mar-lang
make
sudo cp bin/mar /usr/local/bin/mar

```

---

## 4. Compiler CLI Usage

Mar features a dual-mode build system that keeps your project directories clean.

### Run Mode (Scripting)

This mode acts exactly like running a Python or Node.js script. Mar compiles your code to a hidden temporary location, executes the binary, and instantly deletes it. Your folder remains 100% clean.

```bash
mar run app.mar

```

### Build Mode (Native Binaries)

When you are ready to distribute your software, use Build Mode. This creates a lightning-fast native binary executable in your current directory, while silently deleting the intermediate C files.

```bash
mar app.mar
./app          # Execute your native binary!

```

### Tooling & LSP

Mar includes deep inspection tools and a built-in Language Server for IDE integration.

```bash
# Print the generated C code instead of deleting it (useful for debugging)
mar app.mar -o debug_output.c

# Print every token generated by the Lexer
mar app.mar --dump-tokens

# Print the parsed Abstract Syntax Tree (AST) structure
mar app.mar --dump-ast

# Start the JSON-RPC Language Server (used by VS Code / Neovim extensions)
mar lsp

```

---

## 5. Language Reference

### Primitive Types

Mar is strictly statically typed.

| Mar type | C equivalent | Description |
| --- | --- | --- |
| `int` | `int64_t` | 64-bit signed integer. |
| `float` | `double` | 64-bit IEEE 754 floating-point number. |
| `char` | `char` | Single 8-bit character. |
| `bool` | `bool` | Boolean (`true` or `false`). |
| `string` | `const char*` | Immutable UTF-8 string pointer. |
| `int[N]` | `int64_t arr[N]` | Fixed-size array allocated on the stack. |
| `ClassName` | `ClassName*` | Heap-allocated class instance (pointer). |

### Variables & Declarations

Variables must be declared with their type.

```mar
// Single declarations
int x
int y = 10
string name = "Mar"
bool is_active = true

// Arrays
int arr[3] = {1, 2, 3}

// Objects
Player p = new Player(100)

```

**Multi-Variable Declarations**
Mar features a robust AST that allows safely declaring and initializing multiple variables of the same type on a single line.

```mar
int a, b = 5, c
float width = 10.5, height = 20.0, depth
char first = 'A', second = 'B'

```

### Type Casting

Mar prohibits implicit type coercion to prevent silent truncation bugs. Casting is accomplished using a clean, functional syntax.

```mar
int val = 65

// Int to Char
char c = char(val)    // 'A'

// Char to Int
int ascii = int(c)    // 65

// Int to Float
float f = float(val)  // 65.000000

```

### Operators & Expressions

```mar
// Arithmetic
+   -   * /   %

// Comparison
==  !=  <   >   <=  >=

// Logical
&&  ||  !

// Compound Assignment
=   +=  -=  *=  /=  %=

// Advanced
.   // Member access (obj.field)
&   // Address-of (only required for specific C-interop or raw take() calls)

```

### Control Flow

#### If / Else

Braces are required for multi-line blocks, but can be omitted for single-line statements.

```mar
if (score > 100) {
    print("High score!")
} else if (score == 100) {
    print("Exactly 100!")
} else {
    print("Keep trying.")
}

// Single-line
if (game_over) print("Game Over")

```

#### While Loop

Standard pre-condition loops.

```mar
int i = 0
while (i < 10) {
    print(i)
    i += 1
}

```

#### For Range

Python-style range iteration. The `start` bound is inclusive, and the `end` bound is exclusive.

```mar
// Prints 0 through 4
for i in range(0, 5) {
    print(i)
}

```

#### Safe Switch

Colons are omitted. The compiler automatically injects `break` statements at the end of every case block to eliminate fallthrough bugs.

```mar
switch(n) {
    case 1
        print("one")
    case 2
        print("two")
    default
        print("other")
}

```

### Functions & Multi-Return

Functions can be called before they are defined (no forward declarations needed).

```mar
int add(int a, int b) {
    return a + b
}

```

**Multiple Return Values** Mar supports returning multiple values via automatic tuple-struct lowering.

```mar
int, int swap(int a, int b) {
    return b, a
}

int main() {
    int x, int y = swap(10, 20)
    print(x, y)  // Outputs: 20 10
    return 0
}

```

### Arrays & Iteration

Arrays are fixed-size stack allocations. The compiler automatically tracks array lengths using companion variables, allowing for completely safe iteration.

```mar
int scores[5] = {10, 20, 30, 40, 50}

// Query length
int count = len(scores)

// Array traversal: The type of 's' is automatically inferred as 'int'
for s in scores {
    print(s)
}

```

### Strings

Strings are immutable `const char*` arrays. String concatenation dynamically allocates memory from the Mar Runtime Arena, ensuring memory safety.

```mar
string greeting = "Hello"
string name = "World"

// Arena-allocated concatenation
string message = greeting + ", " + name + "!"
print(message)

// Query length
int n = len(greeting)  // 5

```

### Frictionless I/O

Mar completely eliminates `printf` and `scanf` format string boilerplate. The `print()` and `take()` functions utilize C11 `_Generic` macros to auto-infer types.

```mar
int age
float height
char initial

// Take multiple inputs without formatting or ampersands!
take(age, height, initial)

// Print automatically spaces arguments and appends a newline!
print("User info:", age, height, initial)

```

### Object-Oriented Programming

Mar supports classes and inheritance with strictly safe memory semantics.

* All objects are allocated on the Arena via `new`.
* All objects are pointers (pass-by-reference).
* The `init()` method is mandatory and called automatically by `new`.
* Fields are accessed using dot syntax (`obj.field`).

```mar
class Player {
    int health
    int score

    void init(int h) {
        health = h
        score = 0
    }

    void damage(int amount) {
        health -= amount
    }
}

int main() {
    Player p = new Player(100)
    p.damage(20)
    print("Health:", p.health)
    return 0
}

```

#### Inheritance

Classes can inherit fields and methods using `extends`.

```mar
class Animal {
    string name
    void init(string n) { name = n }
    void speak() { print("...") }
}

class Dog extends Animal {
    // Override parent method
    void speak() { print(name, "says woof!") }
}

```

### Null Handling

Mar provides a native `null` literal for safe pointer comparisons.

```mar
Player p = null
if (p == null) {
    print("No player assigned.")
}

```

---

## 6. Standard Library (Stdlib)

Mar includes a highly optimized standard library injected directly into the AST via synthetic C imports. Use `import` at the top of your file.

### `mar/math`

```mar
import "mar/math"

int mar_min(int a, int b)
int mar_max(int a, int b)
int mar_abs(int a)
float mar_sqrt(float a)
float mar_pow(float base, float exp)
float mar_floor(float a)
float mar_ceil(float a)

```

### `mar/str`

```mar
import "mar/str"

int mar_str_len(string s)
int mar_str_cmp(string a, string b)
int mar_str_to_int(string s)
string mar_int_to_str(int n)  // Allocates from Arena

```

### `mar/io`

```mar
import "mar/io"

// Reads entire file into Arena memory
string mar_file_read(string path)

// Overwrites file with content
void mar_file_write(string path, string content)

```

---

## 7. Compiler Architecture (Under the Hood)

Understanding Mar's architecture helps you write better software and contribute to the compiler.

### The Pipeline

1. **Lexer (`src/lexer.c`):** Converts the `.mar` source text into a flat array of structural Tokens.
2. **Parser (`src/parser.c`):** Uses Recursive Descent to build a strict Abstract Syntax Tree (AST). It groups multi-variable declarations into isolated `VarDeclItem` structs.
3. **Codegen (`src/codegen_c.c`):** Walks the AST and emits native C code.
4. **Orchestrator (`src/main.c`):** Interfaces with GCC/Clang via `system()` calls, compiles the binary, and manages file cleanup.

### The Arena Allocator (`src/arena.c`)

Mar guarantees zero memory leaks by utilizing an Arena Allocator (default 8MB).

* When you call `new Player()`, Mar simply bumps a pointer forward inside the Arena.
* String concatenations (`a + b`) are allocated on the Arena.
* At program exit, the OS reclaims the entire Arena instantly. There are no `free()` calls, no Garbage Collector pauses, and no fragmentation.

### C11 `_Generic` & Type Inference

Mar avoids building a heavy, slow internal type-inference engine. Instead, it leverages the C backend.
For `print(a, b)`, the codegen injects a `_mar_print()` macro at the top of the C file:

```c
#define _mar_print(x) _Generic((x), \
    int64_t: printf("%ld", (int64_t)(x)), \
    double:  printf("%lf", (double)(x)), \
    char:    printf("%c", (char)(x)), \
    const char*: printf("%s", (const char*)(x)) \
)

```

This forces the C compiler to resolve the types natively, allowing Mar to remain incredibly fast.

### `__typeof__` Array Traversal

When parsing `for item in arr`, Mar lowers it using GCC/Clang compiler extensions:

```c
for (size_t i = 0; i < (sizeof(arr)/sizeof(arr[0])); i++) {
    __typeof__(arr[0]) item = arr[i];
    // loop body
}

```

---

## 8. Example Programs

### FizzBuzz

```mar
int main() {
    for i in range(1, 101) {
        if (i % 15 == 0) {
            print("FizzBuzz")
        } else if (i % 3 == 0) {
            print("Fizz")
        } else if (i % 5 == 0) {
            print("Buzz")
        } else {
            print(i)
        }
    }
    return 0
}

```

### Prime Number Generator

```mar
int is_prime(int n) {
    if (n < 2) return 0
    int i = 2
    while (i * i <= n) {
        if (n % i == 0) return 0
        i += 1
    }
    return 1
}

int main() {
    print("Primes up to 50:")
    for i in range(2, 51) {
        if (is_prime(i)) {
            print(i)
        }
    }
    return 0
}

```

### File I/O

```mar
import "mar/io"
import "mar/str"

int main() {
    string path = "data.txt"
    mar_file_write(path, "100")
    
    string content = mar_file_read(path)
    int value = mar_str_to_int(content)
    
    print("Read value:", value)
    print("Doubled:", value * 2)
    return 0
}

```

---

## 9. Error Handling & Diagnostics

Mar features precise, formatted error reporting. If you make a syntax mistake, Mar will point to the exact file, line, and column.

```text
ParserError at hello.mar:8:10
  Unexpected token ',' in expression

     8 |    int b=10,c=0
       |             ^

```

| Error Type | Description |
| --- | --- |
| `LexerError` | Invalid characters encountered during tokenization. |
| `ParserError` | Structural issues (missing braces, invalid syntax). |
| `SemanticError` | Type mismatches, undefined variables. |
| `CodegenError` | Internal failures during C translation. |

---

## 10. Contributing Guide

Contributions are welcome! Mar is designed with strict stage separation, making it easy to hack on.

| Task | Files to Edit |
| --- | --- |
| **Add a new Keyword** | `src/lexer.c` (`KEYWORDS` array) |
| **Add an AST Node** | `include/mar/ast.h` (`StmtKind` / `ExprKind`) |
| **Add a Syntax Rule** | `src/parser.c` (`parse_stmt` / `parse_expr`) |
| **Change C Output** | `src/codegen_c.c` (`emit_stmt` / `emit_expr`) |
| **Change CLI Flags** | `src/main.c` (`main`) |

**Getting Started:**

1. Fork the repo and clone it.
2. Run `make debug` to build the compiler with AddressSanitizer enabled.
3. Read `include/mar/ast.h` first — it is the single source of truth for the compiler's data structures.

---

## 11. Roadmap

Mar is actively evolving. Here is the current progress:

* [x] Primitive types (`int`, `float`, `char`, `bool`, `string`)
* [x] Print/Take with automatic type inference (Zero format strings)
* [x] Functional type casting (`char(x)`, `int(y)`)
* [x] Multiple variable declarations (`int a=1, b=2`)
* [x] Arrays with static initializers, `len()`, and `for item in arr`
* [x] Functions with parameters and multiple return values
* [x] Control flow (`if`, `while`, `for-range`, safe `switch`)
* [x] Classes, inheritance, and arena-allocated objects
* [x] Smart workspace cleanup (`mar run` vs `mar build`)
* [x] LSP language server (hover, diagnostics, completion)
* [ ] Closures and anonymous functions
* [ ] Generics / Templates
* [ ] Direct LLVM IR backend (bypassing C entirely)

---

## 12. License & Author

**Author:** Aditya 

**Architecture:** Compiler Frontend targeting C11 Native Binaries

**Platforms:** macOS, Linux, Windows (WSL)

This project is licensed under the MIT License — free to use, modify, distribute, and embed.

```

```