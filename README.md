# Fluxion

Fluxion is an experimental ML-inspired language and LLVM-backed virtual machine
for soft-realtime game, simulation, robotics, and control workloads.

The project is intentionally spec-first. The repository contains the v0.1
language design, reference examples, and a working prototype compiler/JIT for a
small executable subset. That subset is useful for trying the language shape,
running control examples, checking matrix dimensions, emitting LLVM IR, and
experimenting in a REPL.

## Features

- Static type checking for `Int`, `Double`, `Bool`, `String`, `Void`, records,
  type aliases, scalar unit aliases, and matrices.
- ML-like expression syntax with `let`, `if`, function calls, record literals,
  field access, assignment, expression sequences, and bounded `for .. static`
  loops.
- Dimensioned matrix types such as `Matrix[4, 2, Double]` with compile-time
  checks for addition, subtraction, and multiplication compatibility.
- LLVM code generation and ORC JIT execution for programs with
  `func main() -> Int`.
- `check`, `emit-llvm`, `run`, and `repl` CLI commands.
- Built-in runtime helpers for printing scalars, strings, booleans, and
  matrices.
- Optional newline-delimited JSON telemetry through `FLUXION_OTEL` or
  `FLUXION_OTEL_FILE`.
- Runnable control examples, including PID cartpole stabilization and Kalman
  object tracking.
- A v0.1 language specification covering the larger design direction:
  deterministic scheduling, reactors, streams, explicit regions, bounded
  queues, host interop, and safety profiles.

## Project Status

Fluxion is pre-release research software. The executable VM implements only a
deliberately small subset of the full language specification. The broader
reactor, stream, scheduler, region, and host integration model is documented in
`docs/`, but not fully implemented in the compiler yet.

Use the repository to explore the design, run the examples, and contribute
incremental compiler/runtime work.

## Getting Started

### 1. Install Prerequisites

You need:

- CMake 3.20 or newer
- A C++17 compiler
- LLVM development packages with CMake config files
- Git

On Ubuntu or Debian-like systems:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake llvm-dev clang
```

On Windows, install LLVM from the official installer or your package manager,
then pass the LLVM CMake package directory when configuring:

```powershell
cmake -S . -B cmake-build-fluxion-vm -DLLVM_DIR="C:\path\to\LLVM\lib\cmake\llvm"
```

### 2. Build The CLI

From the repository root:

```bash
cmake -S . -B cmake-build-fluxion-vm
cmake --build cmake-build-fluxion-vm --target fluxion
```

If CMake cannot find LLVM, set `LLVM_DIR` explicitly:

```bash
cmake -S . -B cmake-build-fluxion-vm -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
```

### 3. Check A Program

```bash
./cmake-build-fluxion-vm/fluxion check tests/valid/arithmetic.flx
```

On Windows with a Debug generator, the executable may be under a configuration
directory:

```powershell
.\cmake-build-fluxion-vm\Debug\fluxion.exe check tests\valid\arithmetic.flx
```

### 4. Run A Program

```bash
./cmake-build-fluxion-vm/fluxion run examples/runnable/cartpole_pid.flx
```

The cartpole example simulates a small PID controller and prints the final pole
angle and accumulated control effort.

### 5. Try The REPL

```bash
./cmake-build-fluxion-vm/fluxion repl
```

Example session:

```text
fluxion> 1 + 2
3
fluxion> func add(a: Int, b: Int) -> Int = a + b
ok
fluxion> add(20, 22)
42
fluxion> [1, 2; 3, 4] * [5, 6; 7, 8]
[19 22; 43 50]
```

Useful REPL commands:

- `:help` shows REPL help.
- `:decls` prints declarations kept in the session.
- `:clear` removes saved declarations.
- `:quit` exits.

### 6. Emit LLVM IR

```bash
./cmake-build-fluxion-vm/fluxion emit-llvm tests/valid/arithmetic.flx
```

This is useful when working on code generation or verifying the runtime ABI.

### 7. Run The Test Suite

```bash
ctest --test-dir cmake-build-fluxion-vm --output-on-failure
```

The tests cover valid programs, invalid diagnostics, REPL scripts, LLVM IR
emission, telemetry, matrix checks, and runnable examples.

## Tutorial: A First Fluxion Program

Create `hello.flx`:

```fluxion
module Tutorial.Hello

func main() -> Int =
  print_string("hello fluxion");
  0
```

Run it:

```bash
./cmake-build-fluxion-vm/fluxion run hello.flx
```

Fluxion programs start with a `module` declaration. Executable programs must
define `func main() -> Int`; the returned integer becomes the process exit code.

### Add A Function

```fluxion
module Tutorial.Functions

func square(x: Int) -> Int =
  x * x

func main() -> Int =
  print_i32(square(7));
  0
```

Function parameters and return types are explicit. Local expression types are
inferred by the checker.

### Add Records

```fluxion
module Tutorial.Records

struct Pose {
  x: Double,
  y: Double
}

func length_squared(p: Pose) -> Double =
  p.x * p.x + p.y * p.y

func main() -> Int =
  let p = Pose(x: 3.0, y: 4.0) in
  print_f64(length_squared(p));
  0
```

Records are named data types with field access through `.` and construction with
named fields.

### Add Dimensioned Matrices

```fluxion
module Tutorial.Matrices

type State2 = Matrix[2, 1, Double]
type Transform2 = Matrix[2, 2, Double]

func apply(t: Transform2, state: State2) -> State2 =
  t * state

func main() -> Int =
  let t = [1.0, 0.1; 0.0, 1.0] in
  let state = [10.0; 2.0] in
  print_matrix(apply(t, state));
  0
```

The type checker verifies that matrix multiplication dimensions line up. For
example, multiplying a `Matrix[2, 2, Double]` by a `Matrix[3, 1, Double]` is
rejected before execution.

### Add A Bounded Loop

```fluxion
module Tutorial.Loops

func main() -> Int =
  let total = 0 in
  for i in 0..5 static do
    total = total + i
  in
    print_i32(total);
    0
```

Loops in the executable subset are explicitly bounded with `static`, matching
Fluxion's broader goal of making latency-sensitive work visible to the compiler.

### Add Telemetry

```fluxion
module Tutorial.Telemetry

func main() -> Int =
  let span = otel_span_start("tutorial") in
  otel_event_i32("answer", 42);
  otel_span_end(span);
  0
```

Telemetry is disabled by default. Enable JSON output with:

```bash
FLUXION_OTEL=stdout ./cmake-build-fluxion-vm/fluxion run telemetry.flx
```

or write records to a file:

```bash
FLUXION_OTEL_FILE=trace.jsonl ./cmake-build-fluxion-vm/fluxion run telemetry.flx
```

## CLI Reference

```text
fluxion check <file.flx>
fluxion emit-llvm <file.flx>
fluxion run <file.flx>
fluxion run --visualize <file.flx>
fluxion repl [script.repl]
```

`run --visualize` enables the built-in cartpole visualizer hooks used by
`tests/valid/cartpole_visualizer.flx`.

## Repository Layout

```text
src/                 Compiler, type checker, LLVM codegen, JIT, and runtime
docs/                v0.1 design documents and safety/control notes
examples/            Runnable Fluxion examples
examples/cpp/        Comparable C++ control example
tests/valid/         Programs expected to check or run
tests/invalid/       Programs expected to fail checking
tests/repl/          Scripted REPL sessions
```

## Documentation

- [Language specification](docs/language-spec.md)
- [Acceptance scenarios](docs/acceptance-scenarios.md)
- [Simulated robotics sensors](docs/simulated-sensors.md)
- [Simulated actuators](docs/simulated-actuators.md)
- [Control abstractions: PID and MPC](docs/control-abstractions.md)
- [Standards and safety profiles](docs/standards-and-safety-profiles.md)
- [C++ versus Fluxion control review](docs/cpp-vs-fluxion-control-review.md)
- [Runnable examples](examples/)

## Example Programs

- [Cartpole PID](examples/runnable/cartpole_pid.flx)
- [Cartpole PID/MPC](examples/cartpole_pid_mpc.flx)
- [Kalman object tracking](examples/kalman_object_tracking.flx)
- [Runnable Kalman object tracking](examples/runnable/kalman_object_tracking.flx)

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the
development workflow, testing expectations, and project conventions.

## License

Fluxion is available under the MIT License. See [LICENSE](LICENSE).
