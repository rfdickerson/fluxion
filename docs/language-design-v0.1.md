# Fluxion Language Design Document (Draft v0.1)

## Vision

Fluxion is a language and runtime for realtime systems, cyber-physical systems, robotics, telemetry pipelines, state estimation, control systems, world models, and predictive systems.

The central thesis:

> Time, causality, uncertainty, constraints, and physical meaning should be first-class program semantics.

Fluxion is not trying to replace Python for experimentation, C++ for low-level systems, ROS 2 for middleware, Prometheus or Splunk for storage and query, or Simulink for legacy control workflows. Fluxion provides a semantic orchestration and assurance layer for realtime reactive systems.

## Core Design Principles

1. Simple surface syntax: clean like Swift, typed like ML, systems-aware like Rust/C++, control-friendly like Simulink, and reactive like Lingua Franca or Esterel.
2. Rich semantics underneath: the compiler/runtime understands causality, deadlines, freshness, bounded queues, replay, timing, uncertainty, physical units, coordinate frames, and constraints.
3. Explicit boundedness: no unbounded streams, history, allocation, latency assumptions, or overflow behavior by default.
4. Interop-first: Fluxion must interoperate with C++, Python, ROS 2/DDS, ONNX, libtorch, OpenTelemetry, Prometheus, and MPC solvers.

## Core Language Concepts

### Reactors

Reactors are isolated stateful realtime computation units.

```fluxion
reactor KalmanTracker in estimation every 120.hz {
  deadline 750.us

  input measurements: stream<TrackMeasurement> capacity 128 overflow drop_oldest
  input steering: latest<AppliedSteering> max_age 20.ms

  output estimate: stream<TrackEstimate> capacity 16 overflow fault

  state current: TrackEstimate in region reactor

  tick(t) =
    current <- predict(current, steering.current, t.dt)
    emit estimate(current)

  on measurements(m) =
    current <- correct(current, m)
}
```

Reactor properties:

- isolated state
- deterministic local semantics
- typed communication
- explicit timing
- bounded resource assumptions

### Communication Kinds

`latest<T>` has sample-and-hold semantics for state estimates, commands, configuration, and control inputs.

`stream<T>` is an ordered discrete event stream. Stream boundaries must declare bounded capacity and overflow policy.

`history` is bounded temporal memory attached to stream inputs.

```fluxion
input latency: stream<Duration> {
  capacity 1024
  overflow drop_oldest
  history 10.min
}
```

### Pipelines

Fluxion uses causal pipelines instead of verbose phase declarations.

```fluxion
pipeline RocketDocking {
  dynamics -> sensor -> fusion -> guidance -> control -> thruster -> telemetry
}
```

Pipeline bodies may describe DAG fan-out/fan-in:

```fluxion
pipeline Rover {
  simulation -> { camera, lidar, imu }
  { camera, lidar, imu } -> fusion
  fusion -> { planner, localization }
  { planner, localization } -> control
}
```

## Timing Model

Fluxion distinguishes physical time, logical time, event time, and replay time.

```fluxion
reactor Controller in control every 5.ms {
  deadline 500.us
  input estimate: latest<StateEstimate> max_age 20.ms
}
```

## Type System Direction

Fluxion uses ML-style type inference, algebraic data types, parametric generics, traits/protocols, effect typing, physical units, and coordinate frames.

```fluxion
frame World
frame Body
frame Camera

type Position = Vec3<World, Meter>
type Velocity = Vec3<World, MeterPerSecond>
```

Measurements are first-class probabilistic observations:

```fluxion
measurement Position2D<World> {
  stamp: time
  value: Vec2<Meter, World>
  noise: Gaussian<Vec2<Meter, World>>
  source: Camera
}
```

## Effects And Runtime Profiles

Realtime-safe execution is enforced through effects:

```fluxion
func update_pid(...) -> Command
  effects deterministic, bounded_time, no_alloc
```

Runtime profiles define scheduler and allocation contracts:

```fluxion
runtime Realtime {
  scheduler static_priority
  allocation no_heap_after_init
  queues bounded
  replay deterministic
}
```

## Deterministic Replay

Replay records external inputs, timestamps, scheduling decisions, random seeds, dropped messages, and faults.

```bash
fluxion replay mission.flxlog
```

## Contracts

```fluxion
contract {
  assume measurements.age < 40.ms
  guarantee estimate.rate == 120.hz
  invariant covariance is positive_semidefinite
}
```

## Control Systems

Fluxion treats MPC as a schedulable realtime computation, not only a math library.

```fluxion
mpc DockingController {
  model RelativeOrbitalDynamics

  horizon 10.s
  step 100.ms

  minimize FuelUsage

  subject_to {
    docking_corridor
    thrust_limits
  }

  deadline 4.ms
  on_timeout use previous_solution
  on_stale_state enter hold_position
  on_infeasible degrade_to safe_abort
}
```

CBF and CLF declarations express safety and stability surfaces:

```fluxion
cbf CollisionAvoidance(state) =
  distance(state.position, obstacle.position)^2 - safe_radius^2

clf Stability(state) =
  squared_norm(state.position - target.position)
```

## World Models

```fluxion
world_model TerrainPredictor {
  backend onnx "terrain_model.onnx"
}
```

World model backends may include ONNX, libtorch, ExecuTorch, and native autodiff models.

## Telemetry And Observability

Fluxion can ingest Prometheus, OpenTelemetry, Kafka, and DDS:

```fluxion
source prometheus {
  metric p95_latency = histogram_quantile(...)
}
```

## `fluxion-check`

Static analysis checks causality, queue bounds, stale data, unit mismatches, frame mismatches, deadline assumptions, replay sufficiency, covariance correctness, and numerical warnings.

Example diagnostics:

```text
WARN: innovation gate mixes meters and meters/second
WARN: solver fallback may violate safety barrier
OK: all queues bounded
OK: estimator freshness guarantee satisfied
```

## Novelty Claim

Fluxion is not novel as actors plus streams plus timers. It becomes interesting as deterministic reactors plus bounded stream/history semantics plus physical and uncertainty typing plus deadline-aware optimization plus semantic replay plus cross-domain CPS semantics.

## Philosophy

Fluxion is not a better syntax for C++. Fluxion is a semantic systems language for realtime causal computation.

> The compiler understands time, uncertainty, causality, boundedness, and physical meaning as executable semantics rather than comments.
