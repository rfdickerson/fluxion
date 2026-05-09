# C++ Versus Fluxion For CartPole PID/MPC Control

This review compares the C++ reference implementation in `examples/cpp/cartpole_pid_mpc.cpp` with the runnable Fluxion example `examples/cartpole_pid_mpc.flx`. A minimal CMake project is included in `examples/cpp/CMakeLists.txt`.

The C++ version is intentionally competent: it uses fixed-size arrays, fixed logs, a bounded ring buffer, deterministic RNG, explicit actuator saturation, slew limiting, and a bounded MPC horizon. The comparison is therefore not "Fluxion beats careless C++." It is about what the language makes explicit and enforceable.

## What The C++ Implementation Shows

The C++ version implements the same loop:

```text
CartPolePlant.state -> PID + MPC -> ControllerSelector
ControllerSelector -> SimulatedLinearActuator -> CartPolePlant
```

It is fast, portable, and close to what an experienced robotics or controls engineer might write for an embedded simulation prototype. However, several important realtime/control properties are conventions:

- The PID, MPC, actuator, and plant ordering lives in `main`, not in a scheduler-visible graph.
- Bounded memory is achieved by discipline, not by a type or region system.
- Diagnostics are manually threaded through `FixedLog`.
- The difference between requested command and applied actuator effect depends on naming and review discipline.
- Deadlines are comments/config values unless a runtime is built around them.
- Parallel safety is not represented in function signatures or types.

## What Fluxion Makes Better

Fluxion's advantage is not that it can calculate a better control law by magic. Its advantage is that it makes the control architecture part of the program semantics.

| Concern | C++ | Fluxion |
|---|---|---|
| Control graph | Implicit in call order | Explicit reactors, streams, and phases |
| Realtime memory | Manual discipline | Regions, frame allocation, bounded queues |
| Controller state | Class fields | Declared `state` with lifetime |
| PID/MPC output contract | Struct convention | Typed stream contract |
| Actuator realism | Possible but manual | Natural separation of command and applied effect |
| MPC workspace | Local arrays by convention | `region frame` bounded workspace |
| Fallback behavior | Branches in code | Typed diagnostics and deterministic selector |
| Scheduling | Hand-written loop | Phase/dependency scheduler semantics |
| Parallel safety | Informal review | Reactor metadata such as `parallel safe` |
| Queue overflow | Ad hoc container behavior | Required stream capacity and overflow policy |

## Control Systems Assessment

From a control-systems engineering perspective, Fluxion's approach is stronger for closed-loop realtime work because it preserves the structure that control scientists care about:

- The plant, controller, actuator, and observer are separate typed components.
- The sample period is visible at the reactor boundary.
- Actuator limits are represented as part of the signal path, not hidden after the controller.
- MPC resource bounds are part of the controller contract.
- Solver failure is a typed control event with deterministic fallback.
- The scheduler can reason about causality: state sample, control update, actuator application, plant integration, safety check.

That matters because many control bugs are architectural rather than numerical. A controller can be mathematically correct and still fail in deployment because of stale measurements, actuator saturation, variable update order, queue buildup, unbounded allocation, or deadline misses. Fluxion is designed to force those concerns into the source language.

## Where C++ Still Wins

C++ remains stronger today in practical terms:

- mature compilers and debuggers
- numerical libraries
- embedded toolchains
- ROS and simulator integration
- deterministic performance when written carefully
- existing controls ecosystem

Fluxion would need a compiler, runtime, debugger, simulator bridge, and numerical library before it could compete operationally.

## Bottom Line

For a single expert writing a small loop, C++ is sufficient and probably faster to ship. For a large realtime robotics or simulation system with many controllers, sensors, actuators, and scheduling constraints, Fluxion's model is cleaner: it turns implicit control-system architecture into typed, schedulable, memory-bounded program structure.

The best near-term path is hybrid: implement Fluxion so reactors can compile to efficient C++ or native code, then use C++ libraries for numerics while Fluxion owns the realtime graph, controller contracts, memory regions, and deterministic scheduling.
