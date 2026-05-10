#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" int fluxion_print_f64(double value);
extern "C" int fluxion_print_i32(int value);
extern "C" int fluxion_print_bool(bool value);
extern "C" int fluxion_print_string(const char* value);
extern "C" int fluxion_print_matrix(void* matrix);
extern "C" int fluxion_pow_i32(int base, int exponent);
extern "C" double fluxion_pow_f64(double base, double exponent);
extern "C" double fluxion_math_sin(double value);
extern "C" double fluxion_math_cos(double value);
extern "C" double fluxion_math_tan(double value);
extern "C" double fluxion_math_asin(double value);
extern "C" double fluxion_math_acos(double value);
extern "C" double fluxion_math_atan(double value);
extern "C" double fluxion_math_atan2(double y, double x);
extern "C" double fluxion_math_sqrt(double value);
extern "C" double fluxion_math_exp(double value);
extern "C" double fluxion_math_log(double value);
extern "C" double fluxion_math_log10(double value);
extern "C" void* fluxion_matrix_create(int rows, int cols);
extern "C" int fluxion_matrix_set(void* matrix, int row, int col, double value);
extern "C" void* fluxion_matrix_add(void* lhs, void* rhs);
extern "C" void* fluxion_matrix_sub(void* lhs, void* rhs);
extern "C" void* fluxion_matrix_mul(void* lhs, void* rhs);
extern "C" int fluxion_viz_cartpole(double stamp, double cart_x, double pole_theta);
extern "C" double fluxion_viz_cartpole_shove(double magnitude);
extern "C" int fluxion_otel_span_start(const char* name);
extern "C" int fluxion_otel_span_end(int span_id);
extern "C" int fluxion_otel_event_i32(const char* name, int value);
extern "C" int fluxion_otel_event_f64(const char* name, double value);

namespace fluxion {

void set_cartpole_visualizer_enabled(bool enabled);
void set_pretty_output_enabled(bool enabled);

enum class ReactorRuntimeMode {
  Serial,
  DeterministicParallel,
};

struct ReactorRuntimeOptions {
  ReactorRuntimeMode mode = ReactorRuntimeMode::DeterministicParallel;
  std::size_t worker_count = 0;
  std::size_t max_reactors = 64;
  std::size_t reactor_arena_bytes = 64 * 1024;
  std::size_t frame_arena_bytes = 64 * 1024;
};

class ReactorArena {
 public:
  explicit ReactorArena(std::size_t capacity_bytes);

  void* allocate(std::size_t bytes, std::size_t alignment);
  void reset();
  std::size_t used() const;
  std::size_t capacity() const;

 private:
  std::vector<std::uint8_t> storage_;
  std::size_t offset_ = 0;
};

struct ReactorContext {
  std::size_t reactor_index = 0;
  std::uint64_t tick = 0;
  double logical_time_seconds = 0.0;
  ReactorArena* reactor_arena = nullptr;
  ReactorArena* frame_arena = nullptr;
};

struct ReactorTask {
  std::string name;
  std::string phase;
  int priority = 0;
  bool parallel_safe = false;
  double period_seconds = 0.0;
  std::function<void(ReactorContext&)> initialize;
  std::function<void(ReactorContext&)> tick;
  std::function<void(ReactorContext&)> commit;
};

class DeterministicReactorRuntime {
 public:
  explicit DeterministicReactorRuntime(ReactorRuntimeOptions options = {});
  ~DeterministicReactorRuntime();

  DeterministicReactorRuntime(const DeterministicReactorRuntime&) = delete;
  DeterministicReactorRuntime& operator=(const DeterministicReactorRuntime&) = delete;

  void add_reactor(ReactorTask task);
  void initialize();
  void run_ticks(std::uint64_t count, const std::function<void(std::uint64_t, double)>& after_tick = {});
  std::size_t reactor_count() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fluxion
