# Plus++ Compiler

A two-phase compiler for the **Plus++** programming language, written in C. Developed as a course project for **Programming Languages** at Ege University.

## Project 1 – Lexical Analyzer

A lexer that tokenizes Plus++ source code (`.plus` files) into a stream of tokens (`.lx` files).

**Features:**
- Recognizes keywords, identifiers, operators, literals, and comments
- Error detection for invalid characters, unterminated strings, and malformed comments
- Outputs structured token stream

### Usage

```bash
cd Project1-Lexer/src
gcc -o lexer la.c
./lexer ../samples/myscript.plus
```

## Project 2 – Parser & Interpreter

A recursive descent parser that builds an AST from Plus++ source code and interprets it with big integer arithmetic support.

**Language Features:**
- Variable declarations (`number x;`)
- Assignment (`:=`), increment (`+=`), decrement (`-=`)
- Big integer arithmetic (arbitrary precision)
- Loop constructs (`repeat`)
- Output statements (`write ... and newline;`)

### Sample Program

```
number a;
number b;
number result;

a := 12345;
b := -67890;
result := a;
result += b;

write "Result is:" and result and newline;
```

### Usage

```bash
cd Project2-Parser/src
gcc -o parser prs.c
./parser ../samples/test_variables.ppp
```

## Project Structure

```
Project1-Lexer/
├── src/
│   └── la.c               # Lexical analyzer
├── samples/                # Test .plus files + expected .lx outputs
└── docs/
    └── Report.pdf
Project2-Parser/
├── src/
│   └── prs.c              # Parser & interpreter
├── samples/                # Test .ppp programs
└── docs/
    └── Project Report.pdf
```

## Tech Stack

- C (gcc)

## Authors

Ege University - Computer Engineering, 2024-2025
