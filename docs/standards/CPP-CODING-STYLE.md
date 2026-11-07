# CPP-CODING-STYLE.md - C++ Coding Standards and Project Rules

This document defines the rules that all agents, including AI agents,
must follow when writing, reviewing, or modifying code in this
project.

We are writing modern C++ code.

- All development and debugging happens locally on Linux with GCC.
- GCC builds must use full warnings and sanitizers while debugging.

## 1. Project Directory Structure

Do not create files outside the approved project directories.
Do not leave editor backups, stray object files, or temporary files in
the tree.

- `src/` contains application `.cpp` files.
- `include/` contains top-level public headers that belong to the root project.
- `lib/` contains **host** libraries only: code compiled with the host
  compiler and linked into the toolchain's own programs, plus
  third-party libraries of that kind.
- `lib/<name>/` contains the implementation of a library such as
  `lib/dbf/`.
- `lib/<name>/include/` contains library-local public headers.
- `target/` contains everything that runs on the Z80 and is built with
  `nas` and `ncc`, never with the host compiler (see "Target code"
  below). Nothing in `target/` is linked into a host program.
- `bin/` contains final programs and other final generated outputs.
- `build/` contains all build artifacts, including object files,
  dependency files, static libraries, maps, listings, and temporary
  build products.
- `docs/` contains project notes and design documents.
- `tests/<category>` contains automated tests of category.

Target code (`target/`):

- `target/libc/` is the portable C11 library: headers in
  `target/libc/include/`, sources in `target/libc/src/`, no assembly.
  It reaches the machine only through the platform interface it
  declares (`__putchar`, `__open`, `__exit`, ...).
- `target/cpu/<cpu>/` (`z80`, `z80n`) holds assembly that depends only
  on the instruction set. `z80n` overrides `z80` file by file, and only
  where the override is measurably faster.
- `target/platform/<name>/` (`zx-next`, `zx-128`, `idp`, `msx`, `cpc`)
  holds what depends on the machine: bank protocol (`bank.inc` and the
  `__far_*` routines), startup, console and file glue, hardware
  headers, and `platform.conf` (CPU, memory layout, usable banks).
  Platform directory names are lowercase with dashes; `--platform=<name>`
  selects them.
- `target/common/` holds code shared by several platforms.
- Layers depend downwards only: platform on cpu and libc, cpu on
  nothing, libc on nothing but the platform interface.
- A target directory has the same shape as a library: `include/`,
  `src/`, and its own `Makefile`. Public headers of a platform live in
  `target/platform/<name>/include/<name>/`.
- Target output goes to `bin/target/<cpu>/lib/` (libc and cpu
  routines, built once per CPU) and `bin/target/<platform>/` (`lib/`,
  `include/`, `crt0.o`); intermediate files go to `build/target/`.

Rules:

- Public headers must live either in the root `include/` tree, in the
  owning host library's `lib/<name>/include/` tree, or in the owning
  `target/` directory's `include/` tree.
- Project source files may live in `src/`, in a library directory
  under `lib/`, or in `target/`.
- Build artifacts must go only into the root `build/` directory.
- No nested `build/` directories are allowed.
- Nothing else should be created at the project root except normal
  project files such as `LICENSE`, `README.md`, `Makefile`, and similar
  top-level metadata. Coding standards now live under `docs/standards/`.

## 2. Naming Conventions

- Use lowercase snake_case for C/C++ symbols including classes, etc.
- Do not use Hungarian notation, PascalCase, or camelCase.
- Use uppercase only when a true macro-style constant makes sense.
- Use lowercase snake_case for file names.

Examples:

- Good: `screen_clear()`, `player_score`, `dbf_open()`
- Bad: `ScreenClear()`, `playerScore`, `DbfOpen()`

## 3. Header and Implementation Separation

Every module must follow a clear header-and-implementation layout.

- A public `.h` file in `include/`, `lib/<name>/include/`, or
  `target/libc/include/` contains:
  - A file header comment
  - Public declarations
  - Public types
  - Public constants or macros
  - Documentation for every public function
- A `.cpp` file in `src/` or `lib/<name>/` contains:
  - A file header comment
  - `#include "module.h"`
  - Private static helpers and state
  - The implementation

Do not put function bodies in headers unless a tiny static helper is
truly necessary and clearly marked.

## 4. File Header

Every `.cpp` and `.h` file must begin with a real file header in this
style:

```cpp
//
// Describe exactly what this file does.
// Be specific about the module purpose, important design choices,
// and any hardware assumptions that matter.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//
```

Rules:

- Replace the placeholder text with a real description.
- The comment must describe the file, not just list its symbols.
- Keep lines readable on an 80-column terminal where practical.

## 5. Function Documentation

Every function declared in a public header must have a comment
immediately above its prototype.

Example:

```c
//
// Look up an application id by application name.
//
// Parameters:
//      name        - Application name, usually space-padded in GEM
//                    style.
//
// Returns:
//      Matching AES application id, or a negative value if not found.
//
// Notes:
//      This is commonly used for accessory and desktop integration.
//
// Sample call:
//      WORD shell_id = appl_find("DESKTOP ");
//
WORD appl_find(char *name);
```

Rules:

- Say what the function does.
- Mention parameters and the return value when needed.
- Mention side effects, limits, or hardware requirements when
  relevant.
- Document private static helpers too when their purpose is not
  obvious.

## 6. General Coding Rules

- Fix all warnings under `-Wall -Wextra -pedantic`.
- Prefer small, focused functions.
- Keep source files under about 500 lines when possible.
- Avoid global variables unless they are truly necessary.
- Keep code readable on an 80-column terminal.
- Use 4 spaces for indentation and no tabs.
- Use K&R braces for functions. Control-flow brace style may vary
  within reason, but keep it consistent inside a file.

## 7. Build Rules

- Always build and test with G++ first.
- Use `-g -fsanitize=address,undefined` for hosted debug builds.
- All build outputs must go into the root `build/` directory only.
- Do not create `build/` directories inside `src/`, `lib/`, `tests/`,
  or any other subdirectory.
- Use nested makefiles.
- The top-level `Makefile` should define the main entry points.
- Subdirectories such as `src/`, `lib/`, `lib/dbf/`, `target/`, and
  `tests/` should have their own makefiles when needed.
- `target/` is not built by the host `lib/` makefile. `target/Makefile`
  runs after the tools exist and builds with `ncc` and `nas`, once per
  CPU and once per platform, as described in section 1.
- C in `target/` follows the same rules as the rest of this document
  (naming, file headers, function documentation, 80 columns, no
  tabs). Assembly in `target/` follows ASM-CODING-STYLE.md. Target
  code is tested by running it on `nemu`, in `tests/target/<name>/`.
- Keep VS Code `F5` debugging working through `.vscode/`.
- Fix all warnings if using AI coding.

## 8. Documentation

- Keep `README.md` up to date.
- `README.md` must explain:
  - How to build the project
  - Dependencies
  - Usage
- Keep notes in `docs/`, using the current subfolders:
  `manuals/`, `standards/`, 'notes/' ... manuals are for
  real books, standards have coding and build standards,
  notes are for internals.
- All markdowns must have name in capital letters and .md in small,
  separate words with minuses, keep names short but understandable

## 9. Tests

- Keep automated tests in `tests/`.
- Create testing caegories under tests/<category> for various
  types of tests.
