# Repository Guidelines

## Project Structure & Module Organization

This repository contains three standalone C++17 regex-engine implementations. `src/regex-0.cc` is the recursive matcher, `src/regex-1.cc` implements the Parser-to-AST-to-NFA-to-DFA pipeline, and `src/regex-2.cc` adds character predicates, equivalence classes, Lazy DFA construction, and text segmentation. `README.md` links to the canonical conceptual chapters in the Text book; do not duplicate those chapters here. Keep generated files under `build/`; do not commit build artifacts.

## Build, Test, and Development Commands

Configure and build the supported executables from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/regex-0
./build/regex-1
./build/regex-2
```

The executables run their built-in examples and checks. For stricter compiler diagnostics during development, configure a separate tree with `cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"`.

## Coding Style & Naming Conventions

Use C++17 and follow the style already present in the file being changed. Indent with four spaces, place opening braces on the same line, and keep implementation inside the `regex` namespace. Existing types and methods use `PascalCase` (`RegexParser`, `GetNFA`); local variables and free test helpers generally use `snake_case`. Prefer RAII, standard containers, `std::unique_ptr`, and explicit ownership. No formatter is configured, so keep formatting focused and avoid unrelated reflows.

## Testing Guidelines

There is no external test framework or coverage threshold. Tests currently live in each executable's `main()` and print mismatches with `FAIL`. Add focused cases near related examples, covering successful matches, rejection, empty input, quantifier boundaries, and UTF-8 text where applicable. Run all three CMake-built executables and inspect their complete output before submitting. If adding a new standalone engine, register it in `CMakeLists.txt` so it can be built consistently.

## Commit & Pull Request Guidelines

Recent history uses short, descriptive subjects such as `Add regex engine source, docs, CMake build, and LICENSE`. Write an imperative summary, keep each commit scoped to one logical change, and explain non-obvious algorithm decisions in the body. Pull requests should describe behavioral changes, list commands run, and include representative before/after output for matching or performance changes. Link relevant issues and update `README.md`; propose conceptual tutorial changes in the IsBook repository.
