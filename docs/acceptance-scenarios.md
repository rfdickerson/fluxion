# Fluxion v0.1 Acceptance Scenarios

These scenarios define the minimum behavioral coverage expected from a parser, typechecker, scheduler model, and early runtime prototype.

## 1. Game Input To Movement

Design scenario: game input to movement

Expected behavior:

- `InputCommand` events are consumed in stream order during the `update` phase.
- Multiple commands with the same timestamp are ordered by producer sequence number.
- Player state persists in `region reactor`.
- Movement output is emitted after state update and before later phases.
- If the input queue exceeds capacity, the oldest commands are dropped and a diagnostic fault is emitted.

Scheduler graph:

```text
host.gamepad -> PlayerMovement.controls -> PlayerMovement.moved
phase input before update
PlayerMovement in update
```

## 2. Fixed-Step Physics With Render Interpolation

Design scenario: fixed-step physics with render interpolation

Expected behavior:

- Physics runs at a fixed tick rate independent of render sampling.
- Physics state is updated only in the `physics` phase.
- Render interpolation reads sampled snapshots and does not mutate physics state.
- Frame-region temporary contact buffers are released after each scheduler frame.
- Physics output order is deterministic for identical input snapshots.

Scheduler graph:

```text
InputSnapshot -> PhysicsWorld -> PhysicsSnapshot -> RenderInterpolation
phase update before physics before render
```

## 3. Robotics Sensor Fusion

Design scenario: robotics sensor fusion

Expected behavior:

- IMU and encoder streams are merged by timestamp and deterministic tie-break rules.
- Sensor calibration state persists for the reactor lifetime.
- Actuator command output is emitted only from the `control` phase.
- Overflow on IMU input raises a fault instead of silently dropping data.
- Blocking extern calls are rejected by the typechecker inside realtime handlers.

Scheduler graph:

```text
host.imu + host.encoder -> SensorFusion.pose -> ControlLoop.motor_command
phase input before control
```

## 4. Simulated Robotics Sensors

Design scenario: simulated robotics sensors

Expected behavior:

- Ground truth, IMU, and wheel encoder reactors run at independent fixed tick rates.
- Sensor noise is deterministic for the same simulation seed.
- Bias and random generator state persist in `region reactor`.
- Dropout and saturation emit typed diagnostics without blocking sensor execution.
- Simulated IMU and encoder streams can feed the `SensorFusion` input contract.

Scheduler graph:

```text
ControlCommand -> GroundTruthRobot.truth
GroundTruthRobot.truth -> SimulatedImu.imu + SimulatedWheelEncoders.encoders
SimulatedImu.imu + SimulatedWheelEncoders.encoders -> SensorHealthMonitor.diagnostics
phase input before simulation before sensor before control
```

## 5. CartPole Simulated Actuators

Design scenario: CartPole simulated actuators

Expected behavior:

- Controller output is treated as requested force, not actual force.
- The simulated actuator applies latency, deadband, saturation, slew limiting, and deterministic dropout.
- CartPole dynamics consume only sampled `AppliedForce`.
- Safety diagnostics are emitted for cart bounds, pole angle limits, and non-finite state.
- Command latency uses a bounded reactor-local ring buffer.

Scheduler graph:

```text
CartPolePlant.state -> CartPoleController.force_command
CartPoleController.force_command -> SimulatedLinearActuator.applied_force
SimulatedLinearActuator.applied_force -> CartPolePlant.state
CartPolePlant.state + SimulatedLinearActuator.diagnostics -> CartPoleSafetyMonitor.safety
phase control before actuator before physics before safety
```

## 6. PID And MPC CartPole Control

Program: `examples/cartpole_pid_mpc.flx`

Expected behavior:

- PID and MPC both emit the same `ForceCommand` contract.
- PID integral and previous-error state persist in `region reactor`.
- MPC allocates fixed-size horizon, rollout, and scratch buffers from `region frame`.
- MPC fallback is deterministic when the solve is infeasible or exceeds its deadline budget.
- `ControllerSelector` chooses MPC, PID, or fallback commands without ambiguous ordering.
- The existing `SimulatedLinearActuator` can consume the selected command stream.

Scheduler graph:

```text
CartPolePlant.state + CartPoleTarget -> CartPolePidController.command
CartPolePlant.state + CartPoleTarget + CartPolePidController.command -> CartPoleMpcController.command
CartPolePidController.command + CartPoleMpcController.command + diagnostics -> ControllerSelector.selected
ControllerSelector.selected -> SimulatedLinearActuator.command
phase control before actuator
```

## 7. Kalman Object Tracking

Design scenario: reactor-style Kalman object tracking

Expected behavior:

- Position and velocity sensors publish typed `TrackMeasurement` messages with explicit `H` and `R` matrices.
- The tracker keeps the state vector and covariance matrix in `region reactor`.
- Prediction consumes the sampled applied actuator effect, not the requested command.
- Correction uses bounded matrix operations for innovation, innovation covariance, gain, state, and covariance updates.
- The controller and actuator communicate through bounded command/effect streams.

Scheduler graph:

```text
GroundTruthObject.truth -> CameraPositionSensor.measurements + DopplerVelocitySensor.measurements
CameraPositionSensor.measurements + DopplerVelocitySensor.measurements -> KalmanObjectTracker.estimate
KalmanObjectTracker.estimate + TrackTarget -> TrackInterceptController.command
TrackInterceptController.command -> SimulatedSteeringActuator.applied_steering
SimulatedSteeringActuator.applied_steering -> GroundTruthObject.truth + KalmanObjectTracker.estimate
phase simulation before sensor before estimation before control before actuator
```

## 8. C++ PID/MPC Comparison

Program: `examples/cpp/cartpole_pid_mpc.cpp`

Expected behavior:

- The C++ program mirrors the Fluxion PID, MPC, selector, actuator, and plant loop.
- The implementation uses fixed-size arrays/logs and bounded ring buffers rather than dynamic allocation in the control path.
- The comparison document identifies which realtime/control properties are enforced by Fluxion and only conventional in C++.
- The review remains balanced: C++ wins on ecosystem and implementation maturity; Fluxion wins on explicit control graph semantics.

Scheduler graph represented manually in C++:

```text
main loop:
  PID.tick + MPC.tick -> ControllerSelector.select
  ControllerSelector.selected -> SimulatedLinearActuator.tick
  SimulatedLinearActuator.applied -> CartPolePlant.tick
  CartPolePlant.check_safety
```

## 9. Optional Standards And Safety Profiles

Design scenario: optional standards and safety profiles

Expected behavior:

- `FlightSafety` requires hazard ids, safe states, bounded command streams, and explicit safety gates for hazardous commands.
- `EngineSafetyGate` prevents unsafe engine commands when the engine is unhealthy or the mode is `EngineCutoff`.
- `MarketAccess` requires orders to pass through a pre-trade risk gate before the order gateway.
- The compiler emits profile evidence: scheduler graph, region memory map, stream capacities, safety gates, and replay manifest.
- Profiles reject missing capacity, missing hazard ids on critical reactors, unbounded loops, blocking extern calls, and direct hazardous command emission.

Profile graphs:

```text
FlightSafety:
  VehicleState + LandingMode -> DescentGuidance.requested_engine
  DescentGuidance.requested_engine + VehicleState + LandingMode -> EngineSafetyGate.engine_command

MarketAccess:
  strategy decisions -> PreTradeRiskGate.accepted -> order gateway
```

## 10. Bounded Async Fan-Out/Fan-In

Design scenario: bounded async fan-out/fan-in

Expected behavior:

- Job dispatch uses bounded queues.
- Worker reactors may run in parallel because they are marked `parallel safe`.
- Fan-in output is deterministic because results are reduced by job id order.
- Queue overflow uses `backpressure pause_upstream`.
- Nondeterministic worker completion order does not change final reduced output order.

Scheduler graph:

```text
JobSource -> Worker[0..N] -> ResultReducer -> reduced
phase update
```

## 11. Host Callback Feeding Events

Design scenario: host callback feeding events

Expected behavior:

- C callback data enters through an `extern c event source`.
- Host-borrowed payloads are copied or validated before callback return.
- Callback queue capacity is bounded.
- Realtime handlers may call only extern functions marked `realtime`.
- Exported records have stable C-compatible layout.

Scheduler graph:

```text
C host callback -> HostPackets.packet -> PacketConsumer
phase input before update
```

## Negative Scenarios

The compiler or runtime must reject:

- `stream T` crossing reactor boundaries without capacity.
- unbounded allocation inside realtime handlers.
- blocking extern calls inside `on event` or `on tick`.
- shared mutable state between `parallel safe` reactors.
- non-exhaustive variant matches without `_`.
- ambiguous phase ordering between connected reactors.
- `parallel nondeterministic` output feeding deterministic control without an ordering boundary.
- host-borrowed data escaping its declared lifetime.
