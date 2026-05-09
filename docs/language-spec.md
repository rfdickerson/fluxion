# Fluxion Language Specification v0.1

## 1. Manifesto

Fluxion is designed for systems where time is part of the program: game engines, simulation engines, robotics loops, sensor pipelines, and host applications that cannot tolerate unpredictable pauses. The language treats streams, events, scheduling, and memory lifetime as explicit design elements instead of library conventions.

The v0.1 design follows five principles:

1. Predictable latency matters more than maximum average throughput.
2. Event flow should be visible in types and module interfaces.
3. Concurrency should be deterministic unless nondeterminism is requested.
4. Realtime paths must not hide unbounded allocation, blocking, or scheduling side effects.
5. Host integration must be simple enough for C engines, robotics middleware, and simulation runtimes.

Fluxion targets soft realtime. It helps programs avoid latency hazards, but it does not attempt formal hard realtime deadline proofs in v0.1.

## 2. Lexical And Syntax Model

Fluxion uses an ML-like expression syntax with explicit public boundaries. Modules, reactors, extern declarations, and public functions require type annotations. Local expressions may use inference when the inferred type does not escape the declaration.

```fluxion
module Example

type Vec2 = { x: f32, y: f32 }

variant Input =
  | Move(Vec2)
  | Jump

fn length(v: Vec2) -> f32 =
  sqrt(v.x * v.x + v.y * v.y)
```

Names are case-sensitive. Type and variant constructor names use upper camel case by convention. Values, functions, fields, streams, and reactors use lower snake case.

### 2.1 Core Constructs

The v0.1 language includes these public constructs:

- `module`: a namespace and compilation unit.
- `type`: a named alias, record type, or variant type.
- `record`: named product data, usually written with `{ field: Type }`.
- `variant`: tagged union with pattern matching.
- `reactor`: stateful dataflow unit with inputs, outputs, state, and handlers.
- `stream`: a typed sequence of timestamped events.
- `event`: one payload occurrence on a stream.
- `state`: reactor-local persistent storage.
- `on`: handler declaration for events, ticks, lifecycle hooks, and faults.
- `emit`: produce an event or command.
- `phase`: deterministic scheduler phase.
- `region`: explicit allocation lifetime.
- `extern`: C ABI declaration or host integration boundary.

### 2.2 Grammar Sketch

This grammar is intentionally small and implementation-oriented. It defines the surface needed for a v0.1 parser prototype; later specs can refine precedence and optional punctuation.

```text
module        ::= "module" ModuleName item*
item          ::= type_decl | fn_decl | reactor_decl | phase_decl | extern_decl
type_decl     ::= "type" TypeName "=" type_expr
type_expr     ::= primitive | TypeName | record_type | variant_type | "stream" type_expr | "command" type_expr
record_type   ::= "{" field_decl ("," field_decl)* "}"
variant_type  ::= variant_case+
variant_case  ::= "|" ConstructorName ("(" type_expr ")")?
fn_decl       ::= "fn" name "(" params? ")" "->" type_expr "=" expr
phase_decl    ::= "phase" name ("before" name | "after" name)?
reactor_decl  ::= "reactor" TypeName scheduler_meta* "{" reactor_item* "}"
reactor_item  ::= input_decl | output_decl | state_decl | handler_decl
input_decl    ::= "input" name ":" input_kind capacity? overflow? backpressure?
output_decl   ::= "output" name ":" output_kind capacity? overflow?
state_decl    ::= "state" name ":" type_expr "in" "region" region_name
handler_decl  ::= "on" handler_target "=" expr
extern_decl   ::= "extern" "c" extern_safety? extern_item
```

Whitespace is insignificant except where it separates tokens. Newlines may terminate declarations, but block structure is determined by braces and explicit delimiters.

## 3. Type System

Fluxion is statically typed. Type checking must complete before execution. Public reactor interfaces, exported functions, exported types, and extern declarations must have explicit types.

### 3.1 Primitive Types

Required primitive types:

- `bool`
- signed integers: `i8`, `i16`, `i32`, `i64`
- unsigned integers: `u8`, `u16`, `u32`, `u64`
- floats: `f32`, `f64`
- `time`
- `duration`
- `unit`

Numeric conversions are explicit. The compiler must reject implicit narrowing and implicit float-integer conversions.

Domain units may be introduced as nominal aliases over numeric primitives. These aliases keep the same runtime representation as the primitive, but the type checker treats distinct unit names as incompatible unless a literal is used to construct the unit value.

```fluxion
type Meter = Double
type MeterPerSecond = Double

type KinematicSample = {
  x: Meter,
  y: Meter,
  vx: MeterPerSecond,
  vy: MeterPerSecond
}
```

### 3.2 Records

Records are structural within a module and nominal across public module boundaries. A public record exported from a module is treated as a named type.

```fluxion
type Pose = {
  position: Vec3,
  orientation: Quat,
  stamp: time
}
```

### 3.3 Variants And Pattern Matching

Variants are closed sum types. Pattern matches over variants must be exhaustive unless an explicit `_` fallback is present.

```fluxion
variant SensorFault =
  | LostSignal
  | OutOfRange(f32)
  | Timeout(duration)

fn severity(f: SensorFault) -> u8 =
  match f with
  | LostSignal -> 2
  | OutOfRange(v) -> if v > 10.0 then 3 else 1
  | Timeout(_) -> 2
```

### 3.4 Streams And Commands

`stream T` is a typed event stream carrying values of type `T`.

`command T` is an output port used for imperative host-visible effects. Commands are ordered by phase and dependency rules, like stream events, but they are not replayable values.

### 3.5 Dimensioned Matrices

Matrix types may carry compile-time dimensions:

```fluxion
type State4 = Matrix[4, 1, Double]
type Cov4 = Matrix[4, 4, Double]
type Measurement2 = Matrix[2, 1, Double]
type Observation2x4 = Matrix[2, 4, Double]
```

The type checker verifies matrix addition and subtraction use matching dimensions, and matrix multiplication satisfies `lhs.columns == rhs.rows`. A plain `Matrix` annotation remains available when dimensions are intentionally erased.

### 3.6 Type Inference

Local bindings may be inferred:

```fluxion
let speed = length(velocity)
```

The compiler must require annotations when inference would affect public ABI, reactor contracts, region lifetimes, or extern layout.

## 4. Streams And Events

A stream is an ordered sequence of events. Each event has:

- payload value
- logical timestamp
- sequence number within its producer stream
- producing reactor or host source

Event ordering is deterministic. For a single stream, events are observed in producer order. Across streams, order is determined by phase, dependency graph, timestamp, and declaration order as a final tie breaker.

### 4.1 Bounded Queues

Every stream crossing a reactor boundary must declare or inherit a bounded capacity.

```fluxion
input imu: stream ImuSample capacity 256
output pose: stream Pose capacity 64
```

An unbounded stream is illegal in realtime phases. Queue overflow policy must be explicit:

- `drop_oldest`
- `drop_newest`
- `coalesce`
- `fault`

If no policy is specified, the default is `fault`.

### 4.2 Backpressure

Backpressure is represented as scheduler-visible pressure on a stream edge. A reactor may declare how it responds:

```fluxion
input jobs: stream Job capacity 128 overflow fault backpressure pause_upstream
```

Required policies:

- `pause_upstream`: producer is not scheduled for that output until capacity is available.
- `drop_with_fault`: drop according to overflow policy and emit a fault.
- `sample_latest`: retain only the latest value for sampled inputs.

### 4.3 Stream Combinators

The standard library must provide these combinators:

- `map`
- `filter`
- `merge`
- `zip_latest`
- `window_count`
- `window_time`
- `sample_latest`
- `rate_limit`
- `resample_fixed`
- `debounce`
- `coalesce`

Combinators used in realtime phases must preserve bounded memory. A combinator that requires buffering must expose its capacity in the type or declaration.

## 5. Reactors

A reactor is the primary unit of behavior. It defines typed inputs, outputs, local state, lifecycle hooks, and event handlers.

```fluxion
reactor PlayerMovement
  phase update
  tick 60.hz
  priority 20
  parallel safe
{
  input controls: stream InputCommand capacity 32 overflow drop_oldest
  input dt: sampled duration

  output moved: stream Pose capacity 32

  state player: Player in region reactor

  on init(ctx: InitCtx) =
    player <- Player.spawn()

  on event controls(cmd) =
    let next = Player.apply(player, cmd, dt.current)
    player <- next
    emit moved(next.pose)
}
```

### 5.1 Inputs

Reactor inputs are one of:

- `stream T`: ordered event stream.
- `sampled T`: latest value sampled at handler execution time.
- `command T`: host or reactor command input.

### 5.2 Outputs

Reactor outputs are one of:

- `stream T`: replayable ordered event stream.
- `command T`: host-visible imperative command.

### 5.3 State

`state` is reactor-local persistent storage. State must declare a region:

- `region reactor`: persists for the reactor lifetime.
- `region frame`: reset at the end of the current scheduler frame.
- `region pool Name`: allocated from a bounded pool.

Realtime handlers may mutate only their own reactor state or values uniquely owned by the handler. Shared mutable state is illegal unless guarded by a scheduler-recognized synchronization primitive that is proven nonblocking.

### 5.4 Handlers

Required handler forms:

```fluxion
on init(ctx: InitCtx) = ...
on start(ctx: StartCtx) = ...
on tick(t: Tick) = ...
on event input_name(value) = ...
on fault(f: Fault) = ...
on stop(ctx: StopCtx) = ...
```

Handlers run to completion. A realtime handler must not block, allocate from an unbounded heap, wait on an OS mutex, perform unbounded recursion, or call an extern function not marked `realtime`.

## 6. Scheduling

Fluxion uses deterministic phase scheduling by default. A phase is a named scheduler stage with an explicit order.

```fluxion
phase input before update
phase update before physics
phase physics before control
phase control before render
```

Within a phase, the scheduler builds a dependency graph from stream edges, sampled reads, command outputs, and explicit `after` constraints. Independent reactors may run in parallel if they are marked `parallel safe`.

### 6.1 Deterministic Ordering

The scheduler must use this ordering:

1. phase order
2. explicit dependencies
3. logical timestamp
4. producer sequence number
5. declaration order

The runtime may use worker threads, but it must produce the same externally visible event and command order for the same inputs.

### 6.2 Parallel Safety

Reactors declare one of:

- `parallel safe`: handler has no shared mutable side effects and may run concurrently with independent reactors.
- `parallel exclusive`: handler requires exclusive access to declared resources.
- `parallel unsafe`: scheduler must serialize this reactor with all unsafe work in its phase.

Throughput-oriented nondeterminism requires an explicit declaration:

```fluxion
reactor ParticleBatch
  phase update
  parallel nondeterministic
{
  ...
}
```

`parallel nondeterministic` outputs may not feed deterministic realtime control paths unless converted through a declared reduction or ordering boundary.

### 6.3 Scheduler Metadata

Reactors may declare:

- `phase`
- `tick`
- `priority`
- `deadline`
- `affinity`
- `parallel`
- queue `capacity`
- overflow policy

`deadline` is a scheduling hint in v0.1, not a hard realtime proof.

## 7. Memory Regions

Fluxion has no garbage collector in v0.1. Allocation is explicit through regions.

### 7.1 Region Kinds

- `frame`: reset after a scheduler frame.
- `reactor`: lives as long as the reactor instance.
- `pool Name`: bounded pool with fixed capacity or host-provided capacity.
- `host`: borrowed memory owned by the host application.

### 7.2 Allocation Rules

Realtime code may allocate only from declared bounded regions. Allocation failure is a typed possibility unless the region is statically proven sufficient.

```fluxion
let contacts = region frame.alloc_array<Contact>(max_contacts)?
```

The `?` operator propagates typed faults. Unchecked allocation failure is illegal in realtime handlers.

### 7.3 Borrowing And Ownership

Values may be:

- owned by the current region
- borrowed immutably
- borrowed mutably with unique access
- host-owned

A borrowed value may not outlive its region. Host-owned buffers require explicit lifetime annotations at extern boundaries.

## 8. Faults And Errors

Faults are typed recoverable runtime events. Realtime operations that can fail return `Result T Fault` or emit a reactor fault.

Required built-in faults:

- `QueueOverflow`
- `AllocationFailed`
- `DeadlineMissed`
- `ExternFailed`
- `InvalidHostLifetime`
- `BackpressureExceeded`

Unhandled faults in a reactor invoke `on fault`. If no handler exists, the runtime disables the reactor and emits a host diagnostic command.

## 9. C ABI Interop

Fluxion v0.1 supports C ABI interop through `extern`.

```fluxion
extern c realtime fn read_encoder(id: u32) -> Result<f32, ExternFault>

extern c type HostPose = {
  x: f32,
  y: f32,
  z: f32,
  qw: f32,
  qx: f32,
  qy: f32,
  qz: f32
}
```

Extern declarations must specify:

- ABI: only `c` is required in v0.1.
- realtime safety: `realtime` or `blocking`.
- ownership for pointers and buffers.
- layout for exported records.
- error mapping.

Blocking extern calls are illegal from realtime handlers.

### 9.1 Host Callbacks

Host callbacks enter Fluxion through declared event sources:

```fluxion
extern c event source gamepad_packet: stream GamepadPacket
  capacity 128
  overflow drop_oldest
  ownership host_borrowed until_return
```

The runtime must copy or validate host data before the callback returns unless the declaration grants a longer host lifetime.

## 10. Standard Library Shape

The v0.1 standard library should include:

- `Core`: primitives, `Option`, `Result`, comparisons, pattern helpers.
- `Time`: `time`, `duration`, rates, tick utilities.
- `Stream`: bounded stream combinators.
- `Region`: frame/reactor/pool allocation APIs.
- `Collections`: bounded arrays, ring buffers, small maps, priority queues.
- `Math`: vectors, matrices, quaternions, transforms.
- `Diagnostics`: tracing, counters, deadline reporting, fault events.
- `Host`: C ABI bridges, command ports, event source registration.
- `Control`: PID helpers, bounded controller state, fallback selection, and controller diagnostics.
- `Optimization`: fixed-horizon MPC workspaces and realtime-safe bounded solvers.

All realtime-safe standard library functions must be marked as such in their signatures.

## 11. Optional Assurance Profiles

Fluxion can add optional profiles for stronger guarantees without changing the base language. A profile is a named set of compiler restrictions, runtime checks, standard-library subsets, and evidence outputs.

```fluxion
module Guidance.Landing
  use profile FlightSafety

reactor EngineSafetyGate
  assurance critical
  hazard HZ_ENGINE_COMMAND
  safe_state EngineCutoff
{
  ...
}
```

Initial profile families:

- `HardRealtime`: bounded loops, no recursion, no unproven allocation, WCET evidence.
- `SafetyCase`: hazard traceability, safe states, command sequencing, input/output integrity checks.
- `FlightSafety`: mode-state discipline, independent safety gates, restart-to-safe-state behavior.
- `FunctionalSafety`: SIL/ASIL metadata, safety functions, mixed-criticality isolation.
- `UltraLowLatency`: zero-allocation hot paths, fixed layout, affinity and timestamp requirements.
- `MarketAccess`: pre-trade risk gates, order limits, kill switches, deterministic audit/replay.
- `FormalSubset`: total functions, restricted effects, finite-state reactors, proof obligations.

Profiles are opt-in. A program that compiles under a stricter profile also has the ordinary Fluxion semantics; the profile only rejects unsafe patterns or requires additional evidence.

See `docs/standards-and-safety-profiles.md` for the design.

## 12. Implementation Notes

A Fluxion compiler can target LLVM directly or through a native VM that lowers to LLVM-compatible code later. A minimal prototype should implement:

1. lexer and parser for modules, types, functions, reactors, and handlers
2. name resolution and module checking
3. type checking with explicit public boundary annotations
4. region lifetime checking for frame, reactor, pool, and host regions
5. scheduler graph construction from reactors and streams
6. deterministic single-thread runtime
7. parallel runtime after deterministic behavior is proven
8. C ABI import/export stubs

The first runtime should prefer a deterministic single-thread executor with the same phase and dependency semantics as the future multithreaded runtime. This prevents concurrency from becoming part of the language definition before the semantics are stable.
