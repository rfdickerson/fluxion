# Simulated Actuators In Fluxion

Fluxion actuator simulators are reactors that turn controller commands into physical effects. They model the gap between an ideal command and what the simulated plant actually receives: command latency, saturation, slew-rate limits, deadband, efficiency, stiction, faults, and diagnostics.

This document is a design note for future reactor support; it is not backed by a runnable `.flx` example in the current executable subset.

## CartPole Model

The CartPole example is split into four reactors:

- `CartPoleController`: reads sampled state and emits desired force commands.
- `SimulatedLinearActuator`: applies command latency, force limits, slew limits, deadband, and fault injection.
- `CartPolePlant`: integrates cart and pole dynamics from the actual actuator force.
- `CartPoleSafetyMonitor`: emits diagnostics when the simulation leaves configured bounds.

Dataflow:

```text
CartPolePlant.state -> CartPoleController.force_command
CartPoleController.force_command -> SimulatedLinearActuator.applied_force
SimulatedLinearActuator.applied_force -> CartPolePlant.state
CartPolePlant.state + SimulatedLinearActuator.diagnostics -> CartPoleSafetyMonitor.diagnostics
```

## Actuator Semantics

An actuator command is not the same as an actuator effect:

- command: what the controller requested
- applied effect: what the simulated actuator delivered after limits and delay
- diagnostic: why the applied effect differs from the command

This distinction matters for robotics and simulation because controllers should be tested against realistic behavior instead of idealized control signals.

## Realtime Rules

- Command queues are bounded.
- Latency buffers use bounded reactor-local ring buffers.
- Saturation and slew limiting are deterministic.
- Fault injection is deterministic from a reactor-local seed.
- The plant consumes sampled applied force, not controller intent.
- Safety violations are typed diagnostics, not implicit runtime failure.

## Suggested Standard Library Additions

- `Signal.deadband(value, threshold)`.
- `Signal.slew_limit(previous, target, max_delta)`.
- `Signal.clamp(value, min, max)`.
- `Dynamics.cartpole_step(state, force, params, dt)`.
- `Random.bernoulli(rng, probability)`.
- `Collections.RingBuffer<T, N>` for bounded actuator latency.

## Acceptance Criteria

- The same seed and command stream produce identical state and diagnostic streams.
- The actuator never emits force outside configured limits.
- Slew-rate limiting constrains force deltas per tick.
- Command latency is represented by a bounded buffer, not sleeping or blocking.
- The plant state changes only from applied force, never directly from controller command.
- Out-of-bounds cart position or pole angle emits a safety diagnostic.
