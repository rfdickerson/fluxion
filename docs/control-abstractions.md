# Control Abstractions In Fluxion

Fluxion treats controllers as realtime reactors with explicit state, bounded work, and typed failure modes. A controller should expose the same shape whether it is a PID loop, LQR controller, model predictive controller, behavior policy, or hand-written rule.

## Controller Shape

A controller reactor usually has:

- sampled plant state
- sampled target/reference
- bounded command output
- reactor-local controller state
- diagnostics for saturation, stale input, solver fallback, and deadline pressure

```text
PlantState + Reference -> Controller -> Command -> Actuator -> AppliedEffect -> PlantState
```

The key language idea is to separate four concepts that are often collapsed in ordinary code:

- `Reference`: what the system should do.
- `Command`: what the controller asks for.
- `AppliedEffect`: what the actuator actually delivered.
- `PlantState`: what the world did after physics.

This keeps control algorithms composable and testable.

## PID

PID is naturally represented as a stateful reactor:

- proportional term depends on current error
- integral term lives in `region reactor`
- derivative term depends on previous error or measured velocity
- anti-windup clamps the integral state
- output limits are explicit and diagnosed

PID should be the default example for simple control because it is cheap, deterministic, easy to tune, and works well as an actuator fallback when a planner or MPC solver misses a deadline.

## Model Predictive Control

MPC is a bounded optimization problem inside a realtime handler:

- fixed prediction horizon
- fixed number of solver iterations
- preallocated workspace in `region frame` or a named pool
- typed result: optimal command, feasible fallback, or solver fault
- deadline hint and fallback command when the solver cannot finish

Fluxion should not model MPC as arbitrary async work in a realtime loop. The algorithm must declare its bounds so the scheduler and typechecker can reason about memory and execution risk.

## Elegant Language Abstractions

These abstractions would make control code feel native to Fluxion:

- `controller` as syntax sugar over `reactor` for command-producing reactors.
- `objective` blocks for MPC cost terms.
- `constraint` declarations for bounded state and input limits.
- `horizon N step dt` as a compile-time-sized prediction shape.
- `fallback` handlers for solver miss, infeasible plan, or stale state.
- `diagnostic` streams as standard controller outputs.

Example surface syntax for a future spec:

```fluxion
controller CartPoleMpc
  phase control
  tick 200.hz
  horizon 24 step 5.ms
{
  input state: sampled CartPoleState
  input target: sampled CartPoleTarget
  output command: stream ForceCommand capacity 8

  objective minimize
    8.0 * square(pred.pole_theta - target.pole_theta) +
    0.2 * square(pred.cart_x - target.cart_x) +
    0.01 * square(input.force)

  constraint abs(input.force) <= 20.0
  constraint abs(pred.cart_x) <= 2.4

  fallback =
    PidCartPole.command(state.current, target.current)
}
```

The v0.1 examples implement this as ordinary reactors rather than new syntax, so the compiler does not need special controller support yet.

## Acceptance Criteria

- PID and MPC expose the same `ForceCommand` output contract.
- PID keeps integral and previous-error state in `region reactor`.
- MPC uses fixed-size horizon/workspace and no unbounded allocation.
- MPC emits diagnostics for infeasible solve, deadline fallback, and stale plans.
- The actuator consumes controller commands identically regardless of controller type.
- A selector reactor can choose PID, MPC, or fallback commands deterministically.
