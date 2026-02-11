# Repository Guidelines

## Project Structure & Module Organization
- `src/` contains production code, split by domain:
- `src/app/` REPL orchestration (`ShellApp`)
- `src/core/` parsing/tokenization/path resolution
- `src/execution/` process and redirection handling
- `src/builtins/`, `src/history/`, `src/line_editing/` for shell features
- `tests/` holds standalone C++ test executables (`*_tests.cpp`) registered in CMake/CTest.
- `cmake/` contains helper scripts/templates (for example coverage reporting).
- `your_program.sh` is the local run entrypoint used during CodeCrafters development.

## Build, Test, and Development Commands
- `./your_program.sh`: configure + build with vcpkg toolchain, then run `./build/shell`.
- `cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`: configure local build.
- `cmake --build build`: compile the shell and test binaries.
- `cmake --build build --target check`: run all tests through CTest with failure output.
- `ctest --test-dir build --output-on-failure`: rerun tests directly from CTest.
- `cmake -B build-coverage -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='--coverage -O0 -g' -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake && cmake --build build-coverage --target coverage-report`: generate coverage with gcov.

## Coding Style & Naming Conventions
- Language: modern C++ (configured in `CMakeLists.txt`), headers use `#pragma once`.
- Indentation: 4 spaces, braces on the same line for functions/classes.
- Naming: `PascalCase` for types (`ShellApp`), `snake_case` for functions/methods (`execute_pipeline`), trailing underscore for members (`parser_`).
- Keep modules cohesive: parsing changes stay in `src/core/`, execution logic in `src/execution/`, etc.

## Testing Guidelines
- Tests use lightweight assertions (`assert`) in executable-style test files.
- Add/update tests in `tests/<feature>_tests.cpp` for every behavioral change.
- Prefer narrow, behavior-focused test functions (for example parser error cases, redirection variants).
- Run `cmake --build build --target check` before opening a PR.

## Commit & Pull Request Guidelines
- Follow the existing commit tone: short, imperative subjects (for example `fix tokenizer fd redirection boundary parsing`).
- Keep commits scoped to one logical change; include tests with fixes/features.
- PRs should include: change summary, impacted modules, and commands run (`check`, coverage if relevant).
- If tied to a CodeCrafters stage, mention the stage or expected behavior explicitly.
