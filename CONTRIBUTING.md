# Contributing To Fluxion

Thanks for helping improve Fluxion. The project is early-stage, so small,
well-tested changes are easier to review than broad rewrites.

## Development Setup

Install CMake, a C++17 compiler, and LLVM development packages. Then configure
and build:

```bash
cmake -S . -B cmake-build-fluxion-vm
cmake --build cmake-build-fluxion-vm --target fluxion
```

If LLVM is not discovered automatically, pass `LLVM_DIR`:

```bash
cmake -S . -B cmake-build-fluxion-vm -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
```

## Before Opening A Pull Request

Run the full test suite:

```bash
ctest --test-dir cmake-build-fluxion-vm --output-on-failure
```

For parser, type checker, or codegen changes, add or update focused test inputs
under:

- `tests/valid/` for programs that should compile or run.
- `tests/invalid/` for diagnostics that should fail checking.
- `tests/repl/` for REPL behavior.

Then wire new tests into `CMakeLists.txt` with `add_test`.

## Coding Guidelines

- Keep the compiler implementation simple and explicit.
- Prefer diagnostics that point at the source construct the user can fix.
- Keep the executable subset and the v0.1 design spec distinct in docs and code
  comments.
- Use C++17 and avoid adding runtime dependencies unless they are clearly worth
  the cost.
- Keep examples small enough to teach one idea at a time.

## Documentation Guidelines

Update `README.md` when user-facing commands, prerequisites, examples, or
project status change.

Update `docs/language-spec.md` only when the intended language design changes.
If a prototype implementation differs from the spec, document that difference
explicitly instead of silently changing examples.

## Pull Request Checklist

- The change has focused tests or a clear explanation for why tests are not
  needed.
- `ctest --test-dir cmake-build-fluxion-vm --output-on-failure` passes locally.
- New public behavior is documented.
- The PR description calls out any implementation/spec mismatch.
