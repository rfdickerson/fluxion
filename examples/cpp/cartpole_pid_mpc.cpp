#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace cartpole {

constexpr double kDt = 0.005;
constexpr std::size_t kHorizon = 24;

enum class ControllerSource { Pid, Mpc, Fallback };
enum class DiagnosticKind {
  OutputSaturated,
  MpcInfeasible,
  MpcDeadlineFallback,
  ForceSaturated,
  SlewLimited,
  CommandDropped,
  CartOutOfBounds,
  PoleFallen,
  NonFiniteState
};

struct Diagnostic {
  DiagnosticKind kind{};
  ControllerSource source{ControllerSource::Pid};
  double value{};
};

template <typename T, std::size_t Capacity>
class FixedLog {
 public:
  void push(const T& value) {
    if (count_ < Capacity) {
      data_[count_++] = value;
    }
  }

  std::size_t count() const { return count_; }
  const T& operator[](std::size_t index) const { return data_[index]; }
  void clear() { count_ = 0; }

 private:
  std::array<T, Capacity> data_{};
  std::size_t count_ = 0;
};

template <typename T, std::size_t Capacity>
class RingBuffer {
 public:
  void push_drop_oldest(const T& value) {
    if (count_ == Capacity) {
      data_[head_] = value;
      head_ = (head_ + 1) % Capacity;
    } else {
      data_[(head_ + count_) % Capacity] = value;
      ++count_;
    }
  }

  T pop_oldest_or(const T& fallback) {
    if (count_ == 0) {
      return fallback;
    }
    T value = data_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return value;
  }

  std::size_t count() const { return count_; }

 private:
  std::array<T, Capacity> data_{};
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

struct ClampResult {
  double value{};
  bool saturated{};
};

ClampResult clamp(double value, double lo, double hi) {
  if (value < lo) {
    return {lo, true};
  }
  if (value > hi) {
    return {hi, true};
  }
  return {value, false};
}

ClampResult deadband(double value, double threshold) {
  if (std::abs(value) < threshold) {
    return {0.0, value != 0.0};
  }
  return {value, false};
}

ClampResult slew_limit(double previous, double target, double max_delta) {
  return clamp(target, previous - max_delta, previous + max_delta);
}

class Pcg32 {
 public:
  explicit Pcg32(std::uint64_t seed) : state_(seed) {}

  std::uint32_t next_u32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
    const auto rot = static_cast<std::uint32_t>(old >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
  }

  double uniform01() {
    return static_cast<double>(next_u32()) / static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  }

  bool bernoulli(double probability) { return uniform01() < probability; }

 private:
  std::uint64_t state_;
};

struct CartPoleState {
  double stamp{};
  double cart_x{};
  double cart_v{};
  double pole_theta{};
  double pole_omega{};
};

struct CartPoleTarget {
  double cart_x{};
  double cart_v{};
  double pole_theta{};
  double pole_omega{};
};

struct CartPoleParams {
  double cart_mass = 1.0;
  double pole_mass = 0.1;
  double pole_half_length = 0.5;
  double gravity = 9.80665;
  double track_limit_m = 2.4;
  double pole_angle_limit_rad = 0.20944;
};

struct ForceCommand {
  double stamp{};
  double newtons{};
  ControllerSource source{ControllerSource::Pid};
};

struct AppliedForce {
  double stamp{};
  double requested_newtons{};
  double applied_newtons{};
  bool saturated{};
  bool slew_limited{};
  bool deadbanded{};
};

CartPoleState cartpole_step(const CartPoleState& state,
                            double force,
                            const CartPoleParams& params,
                            double dt) {
  const double total_mass = params.cart_mass + params.pole_mass;
  const double polemass_length = params.pole_mass * params.pole_half_length;
  const double sin_theta = std::sin(state.pole_theta);
  const double cos_theta = std::cos(state.pole_theta);
  const double temp = (force + polemass_length * state.pole_omega * state.pole_omega * sin_theta) / total_mass;
  const double theta_acc = (params.gravity * sin_theta - cos_theta * temp) /
                           (params.pole_half_length * (4.0 / 3.0 - params.pole_mass * cos_theta * cos_theta / total_mass));
  const double x_acc = temp - polemass_length * theta_acc * cos_theta / total_mass;

  CartPoleState next = state;
  next.cart_x += dt * state.cart_v;
  next.cart_v += dt * x_acc;
  next.pole_theta += dt * state.pole_omega;
  next.pole_omega += dt * theta_acc;
  next.stamp += dt;
  return next;
}

struct PidGains {
  double kp = 58.0;
  double ki = 1.2;
  double kd = 8.0;
  double integral_limit = 0.8;
  double output_limit = 20.0;
};

struct PidMemory {
  double integral{};
  double previous_error{};
};

class PidController {
 public:
  ForceCommand tick(double now,
                    double dt,
                    const CartPoleState& state,
                    const CartPoleTarget& target,
                    FixedLog<Diagnostic, 32>& diagnostics) {
    const double angle_error = target.pole_theta - state.pole_theta;
    const double cart_error = target.cart_x - state.cart_x;
    const double error = angle_error + 0.15 * cart_error;
    const auto integral = clamp(memory_.integral + error * dt, -gains_.integral_limit, gains_.integral_limit);
    const double derivative = (error - memory_.previous_error) / dt;
    const double raw = gains_.kp * error + gains_.ki * integral.value + gains_.kd * derivative;
    const auto output = clamp(raw, -gains_.output_limit, gains_.output_limit);

    if (output.saturated) {
      diagnostics.push({DiagnosticKind::OutputSaturated, ControllerSource::Pid, raw});
    }

    memory_.integral = integral.value;
    memory_.previous_error = error;
    return {now, output.value, ControllerSource::Pid};
  }

 private:
  PidGains gains_{};
  PidMemory memory_{};
};

struct MpcConfig {
  std::uint32_t max_iterations = 12;
  double force_limit = 20.0;
  double cart_limit = 2.4;
  double deadline_budget_seconds = 0.0018;
};

struct MpcWeights {
  double cart_x = 0.2;
  double cart_v = 0.05;
  double pole_theta = 10.0;
  double pole_omega = 1.5;
  double force = 0.01;
  double force_delta = 0.04;
};

struct MpcPlan {
  double first_force{};
  double cost{};
  bool feasible{};
};

class MpcController {
 public:
  ForceCommand tick(double now,
                    const CartPoleState& state,
                    const CartPoleTarget& target,
                    const ForceCommand& fallback,
                    FixedLog<Diagnostic, 32>& diagnostics) {
    std::array<double, kHorizon> controls{};
    std::array<CartPoleState, kHorizon + 1> rollout{};
    std::array<double, kHorizon> scratch{};

    for (double& u : controls) {
      u = previous_plan_.first_force;
    }

    const auto plan = solve(state, target, controls, rollout, scratch);
    if (!plan.feasible) {
      diagnostics.push({DiagnosticKind::MpcInfeasible, ControllerSource::Mpc, plan.cost});
      return {now, fallback.newtons, ControllerSource::Fallback};
    }

    previous_plan_ = plan;
    return {now, plan.first_force, ControllerSource::Mpc};
  }

 private:
  MpcPlan solve(const CartPoleState& state,
                const CartPoleTarget& target,
                std::array<double, kHorizon>& controls,
                std::array<CartPoleState, kHorizon + 1>& rollout,
                std::array<double, kHorizon>& scratch) const {
    double step = config_.force_limit * 0.5;
    MpcPlan best{controls[0], evaluate(state, target, controls, rollout), true};

    for (std::uint32_t iter = 0; iter < config_.max_iterations; ++iter) {
      bool improved = false;
      for (std::size_t i = 0; i < kHorizon; ++i) {
        const double original = controls[i];
        double best_local = original;
        double best_cost = best.cost;

        for (double candidate : {original - step, original + step}) {
          controls[i] = clamp(candidate, -config_.force_limit, config_.force_limit).value;
          const double cost = evaluate(state, target, controls, rollout);
          if (cost < best_cost) {
            best_cost = cost;
            best_local = controls[i];
            improved = true;
          }
        }

        controls[i] = best_local;
        scratch[i] = best_local;
        best.cost = best_cost;
      }
      step *= 0.5;
      if (!improved) {
        break;
      }
    }

    best.first_force = controls[0];
    best.feasible = std::isfinite(best.cost);
    return best;
  }

  double evaluate(const CartPoleState& initial,
                  const CartPoleTarget& target,
                  const std::array<double, kHorizon>& controls,
                  std::array<CartPoleState, kHorizon + 1>& rollout) const {
    rollout[0] = initial;
    double cost = 0.0;
    double previous_force = controls[0];

    for (std::size_t i = 0; i < kHorizon; ++i) {
      rollout[i + 1] = cartpole_step(rollout[i], controls[i], params_, kDt);
      const CartPoleState& x = rollout[i + 1];
      if (std::abs(x.cart_x) > config_.cart_limit * 1.25 || !std::isfinite(x.pole_theta)) {
        return std::numeric_limits<double>::infinity();
      }

      const double force_delta = controls[i] - previous_force;
      cost += weights_.cart_x * square(x.cart_x - target.cart_x);
      cost += weights_.cart_v * square(x.cart_v - target.cart_v);
      cost += weights_.pole_theta * square(x.pole_theta - target.pole_theta);
      cost += weights_.pole_omega * square(x.pole_omega - target.pole_omega);
      cost += weights_.force * square(controls[i]);
      cost += weights_.force_delta * square(force_delta);
      previous_force = controls[i];
    }

    return cost;
  }

  static double square(double value) { return value * value; }

  CartPoleParams params_{};
  MpcConfig config_{};
  MpcWeights weights_{};
  MpcPlan previous_plan_{};
};

struct LinearActuatorConfig {
  double max_force_n = 20.0;
  double max_slew_n_per_s = 180.0;
  double deadband_n = 0.05;
  std::uint32_t latency_ticks = 2;
  double dropout_probability = 0.0005;
};

class SimulatedLinearActuator {
 public:
  void on_command(const ForceCommand& command) {
    latest_command_ = command;
    delay_.push_drop_oldest(command);
  }

  AppliedForce tick(double now, double dt, FixedLog<Diagnostic, 32>& diagnostics) {
    if (rng_.bernoulli(config_.dropout_probability)) {
      diagnostics.push({DiagnosticKind::CommandDropped, ControllerSource::Fallback, 0.0});
    }

    const ForceCommand delayed =
        delay_.count() > config_.latency_ticks ? delay_.pop_oldest_or(latest_command_) : latest_command_;
    const auto deadbanded = deadband(delayed.newtons, config_.deadband_n);
    const auto clamped = clamp(deadbanded.value, -config_.max_force_n, config_.max_force_n);
    const auto slewed = slew_limit(previous_force_, clamped.value, config_.max_slew_n_per_s * dt);

    if (clamped.saturated) {
      diagnostics.push({DiagnosticKind::ForceSaturated, delayed.source, delayed.newtons});
    }
    if (slewed.saturated) {
      diagnostics.push({DiagnosticKind::SlewLimited, delayed.source, slewed.value});
    }

    previous_force_ = slewed.value;
    return {now, delayed.newtons, slewed.value, clamped.saturated, slewed.saturated, deadbanded.saturated};
  }

 private:
  LinearActuatorConfig config_{};
  Pcg32 rng_{0x0caa7701ULL};
  RingBuffer<ForceCommand, 8> delay_{};
  ForceCommand latest_command_{};
  double previous_force_ = 0.0;
};

class CartPolePlant {
 public:
  const CartPoleState& state() const { return state_; }

  void tick(const AppliedForce& force) {
    state_ = cartpole_step(state_, force.applied_newtons, params_, kDt);
  }

  void check_safety(FixedLog<Diagnostic, 32>& diagnostics) const {
    if (!std::isfinite(state_.cart_x) || !std::isfinite(state_.pole_theta)) {
      diagnostics.push({DiagnosticKind::NonFiniteState, ControllerSource::Fallback, 0.0});
    }
    if (std::abs(state_.cart_x) > params_.track_limit_m) {
      diagnostics.push({DiagnosticKind::CartOutOfBounds, ControllerSource::Fallback, state_.cart_x});
    }
    if (std::abs(state_.pole_theta) > params_.pole_angle_limit_rad) {
      diagnostics.push({DiagnosticKind::PoleFallen, ControllerSource::Fallback, state_.pole_theta});
    }
  }

 private:
  CartPoleParams params_{};
  CartPoleState state_{0.0, 0.0, 0.0, 0.04, 0.0};
};

class ControllerSelector {
 public:
  ForceCommand select(const ForceCommand& pid,
                      const ForceCommand& mpc,
                      const FixedLog<Diagnostic, 32>& diagnostics) {
    for (std::size_t i = 0; i < diagnostics.count(); ++i) {
      const DiagnosticKind kind = diagnostics[i].kind;
      if (kind == DiagnosticKind::MpcInfeasible || kind == DiagnosticKind::MpcDeadlineFallback) {
        mpc_healthy_ = false;
      }
    }

    if (prefer_mpc_ && mpc_healthy_) {
      return mpc;
    }
    return {pid.stamp, pid.newtons, ControllerSource::Fallback};
  }

 private:
  bool prefer_mpc_ = true;
  bool mpc_healthy_ = true;
};

const char* source_name(ControllerSource source) {
  switch (source) {
    case ControllerSource::Pid:
      return "pid";
    case ControllerSource::Mpc:
      return "mpc";
    case ControllerSource::Fallback:
      return "fallback";
  }
  return "unknown";
}

}  // namespace cartpole

int main() {
  using namespace cartpole;

  CartPolePlant plant;
  PidController pid;
  MpcController mpc;
  ControllerSelector selector;
  SimulatedLinearActuator actuator;
  CartPoleTarget target{};
  AppliedForce applied{};
  FixedLog<Diagnostic, 32> diagnostics;

  for (int step = 0; step < 1000; ++step) {
    const double now = step * kDt;
    diagnostics.clear();

    const ForceCommand pid_command = pid.tick(now, kDt, plant.state(), target, diagnostics);
    const ForceCommand mpc_command = mpc.tick(now, plant.state(), target, pid_command, diagnostics);
    const ForceCommand selected = selector.select(pid_command, mpc_command, diagnostics);

    actuator.on_command(selected);
    applied = actuator.tick(now, kDt, diagnostics);
    plant.tick(applied);
    plant.check_safety(diagnostics);

    if (step % 100 == 0) {
      std::cout << std::fixed << std::setprecision(4)
                << "t=" << now
                << " source=" << source_name(selected.source)
                << " command=" << selected.newtons
                << " applied=" << applied.applied_newtons
                << " x=" << plant.state().cart_x
                << " theta=" << plant.state().pole_theta
                << " diagnostics=" << diagnostics.count()
                << '\n';
    }
  }

  return 0;
}
