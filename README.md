# CodeCrafters Shell (C++)

[![progress-banner](https://backend.codecrafters.io/progress/shell/088affdf-fea3-4ce4-a156-0dd9b6f9113b)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)

Implementation of the CodeCrafters **"Build Your Own Shell"** challenge in modern C++.
This shell provides a readline-based REPL, builtin commands, command execution, redirection, and pipelines.

## Features

- Interactive prompt with GNU Readline completion support.
- Builtins: `cd`, `echo`, `pwd`, `type`, `history`, `exit`.
- External command execution via `fork`/`execvp`.
- Pipelines (`|`) across multiple commands.
- Redirection operators: `>`, `>>`, `1>`, `1>>`, `2>`, `2>>`.
- Persistent command history (`HISTFILE`, default `~/.shell_history`).

## Project Layout

- `src/main.cpp`: program entrypoint.
- `src/app/`: REPL loop orchestration.
- `src/core/`: tokenizer, parser, PATH resolution.
- `src/execution/`: process launching and redirection.
- `src/builtins/`, `src/history/`, `src/line_editing/`: shell capabilities.
- `tests/`: executable unit/integration-style tests registered with CTest.

## Prerequisites

- CMake 3.13+
- A recent C++ compiler (project is configured with `CMAKE_CXX_STANDARD 26`)
- GNU Readline development headers/library
- `vcpkg` with `VCPKG_ROOT` set (used by `your_program.sh`)

## Build and Run

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build
./build/shell
```

Or use the helper script:

```sh
./your_program.sh
```

## Tests and Coverage

Run the full test suite:

```sh
cmake --build build --target check
# or
ctest --test-dir build --output-on-failure
```

Generate a gcov report:

```sh
cmake -B build-coverage -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='--coverage -O0 -g' -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
cmake --build build-coverage --target coverage-report
```

## CodeCrafters Workflow

Submit progress to CodeCrafters with:

```sh
git push origin master
```

Local compile/run behavior can be adjusted in `.codecrafters/compile.sh` and `.codecrafters/run.sh`.
