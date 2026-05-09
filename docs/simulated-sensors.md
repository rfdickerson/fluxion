# Simulated Robotics Sensors In Fluxion

Fluxion sensor simulators are ordinary reactors that emit typed streams. They should behave like real sensors: each sensor has its own tick rate, latency, noise, bias, saturation, dropout behavior, and queue policy.

This document is a design note for future reactor support; it is not backed by a runnable `.flx` example in the current executable subset.

## Model

The simulator is split into four reactors:

- `GroundTruthRobot`: produces ideal robot motion from control input.
- `SimulatedImu`: samples ground truth at a high rate and emits noisy acceleration and gyro events.
- `SimulatedWheelEncoders`: samples ground truth at a lower rate and emits noisy wheel distance events.
- `SensorHealthMonitor`: observes emitted samples and raises diagnostics for dropouts, saturation, or stale data.

This keeps the dataflow explicit:

```text
ControlCommand -> GroundTruthRobot.truth
GroundTruthRobot.truth -> SimulatedImu.imu
GroundTruthRobot.truth -> SimulatedWheelEncoders.encoders
SimulatedImu.imu + SimulatedWheelEncoders.encoders -> SensorHealthMonitor.diagnostics
```

## Realtime Rules

- Each sensor output stream is bounded.
- Randomness is deterministic from a reactor-local seed.
- Temporary noise buffers use `region frame`.
- Bias and calibration state live in `region reactor`.
- Dropout does not block; it either skips emission or emits a diagnostic event.
- Saturation is represented in the sample payload so downstream control can react deterministically.

## Suggested Standard Library Additions

These additions are enough for a sensor-simulation prototype:

- `Random.Pcg32`: deterministic fixed-cost pseudorandom generator.
- `Random.normal(rng, mean, stddev)`: bounded-time normal approximation.
- `Noise.bias_walk(rng, current, sigma, limit)`: bounded bias drift.
- `Signal.clamp_with_flag(value, min, max)`: returns `{ value, saturated }`.
- `Diagnostics.SensorDiagnostic`: common diagnostic payload for simulated and real sensors.

## Acceptance Criteria

- Running the same simulation seed produces the same stream events.
- IMU and encoder streams may run at different rates without ambiguous ordering.
- Queue overflow policy is explicit for every stream.
- Dropout and saturation are represented as typed events, not hidden side effects.
- The existing `SensorFusion` example can consume the simulated `ImuSample` and `EncoderSample` streams without changing its input contract.
