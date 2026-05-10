#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

std::atomic<bool>& pretty_output_enabled() {
  static std::atomic<bool> pretty_output_enabled{false};
  return pretty_output_enabled;
}

bool pretty_output_active() {
  if (!pretty_output_enabled().load()) {
    return false;
  }
  return true;
}

bool terminal_color_enabled() {
  if (!pretty_output_active()) {
    return false;
  }
#ifdef _WIN32
  if (_isatty(_fileno(stdout)) == 0) {
    return false;
  }
#else
  if (isatty(fileno(stdout)) == 0) {
    return false;
  }
#endif
  if (std::getenv("NO_COLOR") != nullptr) {
    return false;
  }
  const char* term = std::getenv("TERM");
  return term == nullptr || std::string(term) != "dumb";
}

void set_terminal_pretty_output_enabled(bool enabled) {
  pretty_output_enabled().store(enabled);
}

std::string style_text(const std::string& text, const char* code) {
  if (!terminal_color_enabled()) {
    return text;
  }
  return std::string("\033[") + code + "m" + text + "\033[0m";
}

std::string format_scalar(double value) {
  if (std::abs(value) < 5e-13) {
    value = 0.0;
  }
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return out.str();
}

}  // namespace

extern "C" int fluxion_print_f64(double value) {
  if (!pretty_output_active()) {
    std::cout << value << '\n';
    return 0;
  }
  std::cout << style_text(format_scalar(value), "1;35") << '\n';
  return 0;
}

extern "C" int fluxion_print_i32(int value) {
  std::cout << style_text(std::to_string(value), "1;35") << '\n';
  return 0;
}

extern "C" int fluxion_print_bool(bool value) {
  std::cout << style_text(value ? "true" : "false", value ? "1;32" : "1;31") << '\n';
  return 0;
}

extern "C" int fluxion_print_string(const char* value) {
  std::cout << style_text(value == nullptr ? "" : value, "1;33") << '\n';
  return 0;
}

extern "C" int fluxion_pow_i32(int base, int exponent) {
  if (exponent < 0) {
    return 0;
  }
  int result = 1;
  while (exponent > 0) {
    if ((exponent & 1) != 0) {
      result *= base;
    }
    exponent >>= 1;
    if (exponent > 0) {
      base *= base;
    }
  }
  return result;
}

extern "C" double fluxion_pow_f64(double base, double exponent) {
  return std::pow(base, exponent);
}

extern "C" double fluxion_math_sin(double value) {
  return std::sin(value);
}

extern "C" double fluxion_math_cos(double value) {
  return std::cos(value);
}

extern "C" double fluxion_math_tan(double value) {
  return std::tan(value);
}

extern "C" double fluxion_math_asin(double value) {
  return std::asin(value);
}

extern "C" double fluxion_math_acos(double value) {
  return std::acos(value);
}

extern "C" double fluxion_math_atan(double value) {
  return std::atan(value);
}

extern "C" double fluxion_math_atan2(double y, double x) {
  return std::atan2(y, x);
}

extern "C" double fluxion_math_sqrt(double value) {
  return std::sqrt(value);
}

extern "C" double fluxion_math_exp(double value) {
  return std::exp(value);
}

extern "C" double fluxion_math_log(double value) {
  return std::log(value);
}

extern "C" double fluxion_math_log10(double value) {
  return std::log10(value);
}

namespace {

struct Matrix {
  int rows = 0;
  int cols = 0;
  std::vector<double> values;
};

Matrix* as_matrix(void* matrix) {
  return static_cast<Matrix*>(matrix);
}

void require_same_shape(const Matrix* lhs, const Matrix* rhs, const char* op) {
  if (lhs == nullptr || rhs == nullptr || lhs->rows != rhs->rows || lhs->cols != rhs->cols) {
    throw std::runtime_error(std::string("matrix shape mismatch in '") + op + "'");
  }
}

void print_plain_matrix(const Matrix* m) {
  std::cout << '[';
  for (int r = 0; r < m->rows; ++r) {
    if (r > 0) {
      std::cout << ";\n ";
    }
    for (int c = 0; c < m->cols; ++c) {
      if (c > 0) {
        std::cout << ' ';
      }
      std::cout << m->values[static_cast<std::size_t>(r * m->cols + c)];
    }
  }
  std::cout << "]\n";
}

void print_pretty_matrix(const Matrix* m) {
  std::vector<std::vector<std::string>> cells(static_cast<std::size_t>(m->rows),
                                              std::vector<std::string>(static_cast<std::size_t>(m->cols)));
  std::vector<std::size_t> widths(static_cast<std::size_t>(m->cols), 0);
  for (int r = 0; r < m->rows; ++r) {
    for (int c = 0; c < m->cols; ++c) {
      std::string text = format_scalar(m->values[static_cast<std::size_t>(r * m->cols + c)]);
      widths[static_cast<std::size_t>(c)] = std::max(widths[static_cast<std::size_t>(c)], text.size());
      cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = std::move(text);
    }
  }

  std::cout << style_text(std::to_string(m->rows) + "x" + std::to_string(m->cols) + " Matrix{Float64}:", "1;36")
            << '\n';
  for (int r = 0; r < m->rows; ++r) {
    std::cout << style_text(r == 0 ? "[" : " ", "2");
    for (int c = 0; c < m->cols; ++c) {
      if (c > 0) {
        std::cout << "  ";
      }
      const std::string& cell = cells[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
      std::cout << style_text(std::string(widths[static_cast<std::size_t>(c)] - cell.size(), ' ') + cell, "1;35");
    }
    std::cout << style_text(r + 1 == m->rows ? "]" : ";", "2") << '\n';
  }
}

struct CartPoleVisualizer {
  bool enabled = false;
  bool drew_frame = false;
  double last_shove = 0.0;
  std::mutex lock;
#ifndef _WIN32
  bool raw_mode = false;
  termios original_termios{};
#endif
};

CartPoleVisualizer& cartpole_visualizer() {
  static CartPoleVisualizer visualizer;
  return visualizer;
}

void draw_line(std::vector<std::string>& canvas, int x0, int y0, int x1, int y1, char mark) {
  const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
  if (steps == 0) {
    if (y0 >= 0 && y0 < static_cast<int>(canvas.size()) && x0 >= 0 && x0 < static_cast<int>(canvas[y0].size())) {
      canvas[y0][x0] = mark;
    }
    return;
  }
  for (int i = 0; i <= steps; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(steps);
    const int x = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
    const int y = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
    if (y >= 0 && y < static_cast<int>(canvas.size()) && x >= 0 && x < static_cast<int>(canvas[y].size())) {
      canvas[y][x] = mark;
    }
  }
}

#ifndef _WIN32
void disable_raw_mode() {
  CartPoleVisualizer& visualizer = cartpole_visualizer();
  if (visualizer.raw_mode) {
    tcsetattr(STDIN_FILENO, TCSANOW, &visualizer.original_termios);
    visualizer.raw_mode = false;
  }
}

void enable_raw_mode_if_possible(CartPoleVisualizer& visualizer) {
  if (visualizer.raw_mode || !isatty(STDIN_FILENO)) {
    return;
  }
  if (tcgetattr(STDIN_FILENO, &visualizer.original_termios) != 0) {
    return;
  }
  termios raw = visualizer.original_termios;
  raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
    visualizer.raw_mode = true;
    std::atexit(disable_raw_mode);
  }
}

bool read_byte_nonblocking(char& out) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(STDIN_FILENO, &set);
  timeval timeout{0, 0};
  if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) <= 0) {
    return false;
  }
  return read(STDIN_FILENO, &out, 1) == 1;
}
#endif

double poll_cartpole_shove(double magnitude) {
#ifdef _WIN32
  if (!_kbhit()) {
    return 0.0;
  }
  const int first = _getch();
  if (first != 0 && first != 224) {
    return 0.0;
  }
  const int key = _getch();
  if (key == 75) {
    return -magnitude;
  }
  if (key == 77) {
    return magnitude;
  }
  return 0.0;
#else
  double shove = 0.0;
  char c = '\0';
  while (read_byte_nonblocking(c)) {
    if (c != '\033') {
      continue;
    }
    char bracket = '\0';
    char code = '\0';
    if (!read_byte_nonblocking(bracket) || !read_byte_nonblocking(code) || bracket != '[') {
      continue;
    }
    if (code == 'D') {
      shove = -magnitude;
    } else if (code == 'C') {
      shove = magnitude;
    }
  }
  return shove;
#endif
}

struct OTelSink {
  bool initialized = false;
  bool enabled = false;
  std::unique_ptr<std::ofstream> file;
  std::ostream* stream = &std::cerr;
  std::mutex lock;
  std::atomic<int> next_span_id{1};
};

OTelSink& otel_sink() {
  static OTelSink sink;
  return sink;
}

std::string json_escape(const char* value) {
  std::ostringstream out;
  const std::string text = value == nullptr ? "" : value;
  for (const char c : text) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << c;
        break;
    }
  }
  return out.str();
}

bool truthy(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const std::string text = value;
  return text != "0" && text != "false" && text != "off" && text != "none";
}

OTelSink& ensure_otel_sink() {
  OTelSink& sink = otel_sink();
  std::lock_guard<std::mutex> guard(sink.lock);
  if (sink.initialized) {
    return sink;
  }

  if (const char* path = std::getenv("FLUXION_OTEL_FILE"); path != nullptr && path[0] != '\0') {
    sink.file = std::make_unique<std::ofstream>(path, std::ios::app);
    if (sink.file && *sink.file) {
      sink.stream = sink.file.get();
      sink.enabled = true;
    }
  } else if (const char* target = std::getenv("FLUXION_OTEL"); truthy(target)) {
    const std::string text = target;
    sink.stream = text == "stdout" ? &std::cout : &std::cerr;
    sink.enabled = true;
  }

  sink.initialized = true;
  return sink;
}

void write_otel_record(const std::string& payload) {
  OTelSink& sink = ensure_otel_sink();
  std::lock_guard<std::mutex> guard(sink.lock);
  if (!sink.enabled) {
    return;
  }
  *sink.stream << payload << '\n';
  sink.stream->flush();
}

}  // namespace

extern "C" void* fluxion_matrix_create(int rows, int cols) {
  if (rows <= 0 || cols <= 0) {
    throw std::runtime_error("matrix dimensions must be positive");
  }
  auto* matrix = new Matrix;
  matrix->rows = rows;
  matrix->cols = cols;
  matrix->values.assign(static_cast<std::size_t>(rows * cols), 0.0);
  return matrix;
}

extern "C" int fluxion_matrix_set(void* matrix, int row, int col, double value) {
  auto* m = as_matrix(matrix);
  if (m == nullptr || row < 0 || col < 0 || row >= m->rows || col >= m->cols) {
    throw std::runtime_error("matrix index out of bounds");
  }
  m->values[static_cast<std::size_t>(row * m->cols + col)] = value;
  return 0;
}

extern "C" void* fluxion_matrix_add(void* lhs, void* rhs) {
  auto* a = as_matrix(lhs);
  auto* b = as_matrix(rhs);
  require_same_shape(a, b, "+");
  auto* out = as_matrix(fluxion_matrix_create(a->rows, a->cols));
  for (std::size_t i = 0; i < out->values.size(); ++i) {
    out->values[i] = a->values[i] + b->values[i];
  }
  return out;
}

extern "C" void* fluxion_matrix_sub(void* lhs, void* rhs) {
  auto* a = as_matrix(lhs);
  auto* b = as_matrix(rhs);
  require_same_shape(a, b, "-");
  auto* out = as_matrix(fluxion_matrix_create(a->rows, a->cols));
  for (std::size_t i = 0; i < out->values.size(); ++i) {
    out->values[i] = a->values[i] - b->values[i];
  }
  return out;
}

extern "C" void* fluxion_matrix_mul(void* lhs, void* rhs) {
  auto* a = as_matrix(lhs);
  auto* b = as_matrix(rhs);
  if (a == nullptr || b == nullptr || a->cols != b->rows) {
    throw std::runtime_error("matrix shape mismatch in '*'");
  }
  auto* out = as_matrix(fluxion_matrix_create(a->rows, b->cols));
  for (int r = 0; r < a->rows; ++r) {
    for (int c = 0; c < b->cols; ++c) {
      double sum = 0.0;
      for (int k = 0; k < a->cols; ++k) {
        sum += a->values[static_cast<std::size_t>(r * a->cols + k)] *
               b->values[static_cast<std::size_t>(k * b->cols + c)];
      }
      out->values[static_cast<std::size_t>(r * out->cols + c)] = sum;
    }
  }
  return out;
}

extern "C" int fluxion_print_matrix(void* matrix) {
  auto* m = as_matrix(matrix);
  if (m == nullptr) {
    if (!pretty_output_active()) {
      std::cout << "[]\n";
      return 0;
    }
    std::cout << style_text("[]", "2") << '\n';
    return 0;
  }
  if (!pretty_output_active()) {
    print_plain_matrix(m);
    return 0;
  }
  print_pretty_matrix(m);
  return 0;
}

namespace fluxion {

void set_pretty_output_enabled(bool enabled) {
  set_terminal_pretty_output_enabled(enabled);
}

void set_cartpole_visualizer_enabled(bool enabled) {
  CartPoleVisualizer& visualizer = cartpole_visualizer();
  std::lock_guard<std::mutex> guard(visualizer.lock);
  visualizer.enabled = enabled;
  visualizer.drew_frame = false;
  visualizer.last_shove = 0.0;
#ifndef _WIN32
  if (enabled) {
    enable_raw_mode_if_possible(visualizer);
  } else {
    disable_raw_mode();
  }
#endif
}

ReactorArena::ReactorArena(std::size_t capacity_bytes) : storage_(capacity_bytes, 0) {}

void* ReactorArena::allocate(std::size_t bytes, std::size_t alignment) {
  if (bytes == 0) {
    return nullptr;
  }
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    throw std::runtime_error("reactor arena alignment must be a power of two");
  }
  const std::size_t aligned = (offset_ + alignment - 1) & ~(alignment - 1);
  if (aligned > storage_.size() || bytes > storage_.size() - aligned) {
    throw std::runtime_error("reactor arena exhausted");
  }
  offset_ = aligned + bytes;
  return storage_.data() + aligned;
}

void ReactorArena::reset() {
  offset_ = 0;
}

std::size_t ReactorArena::used() const {
  return offset_;
}

std::size_t ReactorArena::capacity() const {
  return storage_.size();
}

namespace {

struct ScheduledReactor {
  ReactorTask task;
  std::size_t sequence = 0;
  ReactorArena reactor_arena;
  ReactorArena frame_arena;
};

bool reactor_precedes(const ScheduledReactor& lhs, const ScheduledReactor& rhs) {
  if (lhs.task.phase != rhs.task.phase) {
    return lhs.task.phase < rhs.task.phase;
  }
  if (lhs.task.priority != rhs.task.priority) {
    return lhs.task.priority > rhs.task.priority;
  }
  return lhs.sequence < rhs.sequence;
}

class BoundedWorkerPool {
 public:
  BoundedWorkerPool(std::size_t workers, std::size_t max_jobs) {
    queue_.reserve(max_jobs);
    threads_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
      threads_.emplace_back([this] { worker_loop(); });
    }
  }

  ~BoundedWorkerPool() {
    {
      std::lock_guard<std::mutex> guard(lock_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  void run(const std::vector<std::function<void()>>& jobs, std::size_t first, std::size_t last) {
    if (first >= last) {
      return;
    }
    {
      std::lock_guard<std::mutex> guard(lock_);
      remaining_ = last - first;
      failure_ = nullptr;
      queue_.clear();
      queue_head_ = 0;
      for (std::size_t i = first; i < last; ++i) {
        queue_.push_back(&jobs[i]);
      }
    }
    ready_.notify_all();
    std::unique_lock<std::mutex> guard(lock_);
    done_.wait(guard, [this] { return remaining_ == 0; });
    if (failure_ != nullptr) {
      std::rethrow_exception(failure_);
    }
  }

 private:
  void worker_loop() {
    for (;;) {
      const std::function<void()>* job = nullptr;
      {
        std::unique_lock<std::mutex> guard(lock_);
        ready_.wait(guard, [this] { return stopping_ || queue_head_ < queue_.size(); });
        if (stopping_ && queue_head_ >= queue_.size()) {
          return;
        }
        job = queue_[queue_head_++];
      }
      try {
        (*job)();
      } catch (...) {
        std::lock_guard<std::mutex> guard(lock_);
        if (failure_ == nullptr) {
          failure_ = std::current_exception();
        }
      }
      {
        std::lock_guard<std::mutex> guard(lock_);
        --remaining_;
        if (remaining_ == 0) {
          done_.notify_one();
        }
      }
    }
  }

  std::vector<std::thread> threads_;
  std::mutex lock_;
  std::condition_variable ready_;
  std::condition_variable done_;
  std::vector<const std::function<void()>*> queue_;
  std::size_t queue_head_ = 0;
  std::size_t remaining_ = 0;
  bool stopping_ = false;
  std::exception_ptr failure_;
};

std::size_t choose_worker_count(const ReactorRuntimeOptions& options) {
  if (options.mode == ReactorRuntimeMode::Serial) {
    return 0;
  }
  if (options.worker_count > 0) {
    return options.worker_count;
  }
  const unsigned hardware = std::thread::hardware_concurrency();
  return std::max<std::size_t>(1, hardware == 0 ? 1 : hardware - 1);
}

}  // namespace

struct DeterministicReactorRuntime::Impl {
  explicit Impl(ReactorRuntimeOptions runtime_options)
      : options(std::move(runtime_options)), worker_count(choose_worker_count(options)) {
    reactors.reserve(options.max_reactors);
    jobs.reserve(options.max_reactors);
    if (worker_count > 0) {
      workers = std::make_unique<BoundedWorkerPool>(worker_count, options.max_reactors);
    }
  }

  ReactorRuntimeOptions options;
  std::size_t worker_count = 0;
  std::vector<ScheduledReactor> reactors;
  std::vector<std::function<void()>> jobs;
  std::unique_ptr<BoundedWorkerPool> workers;
  bool initialized = false;
};

DeterministicReactorRuntime::DeterministicReactorRuntime(ReactorRuntimeOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

DeterministicReactorRuntime::~DeterministicReactorRuntime() = default;

void DeterministicReactorRuntime::add_reactor(ReactorTask task) {
  if (impl_->initialized) {
    throw std::runtime_error("cannot add reactor after runtime initialization");
  }
  if (impl_->reactors.size() >= impl_->options.max_reactors) {
    throw std::runtime_error("reactor runtime capacity exceeded");
  }
  if (!task.tick) {
    throw std::runtime_error("reactor '" + task.name + "' has no tick handler");
  }
  ScheduledReactor scheduled{std::move(task),
                             impl_->reactors.size(),
                             ReactorArena(impl_->options.reactor_arena_bytes),
                             ReactorArena(impl_->options.frame_arena_bytes)};
  impl_->reactors.push_back(std::move(scheduled));
}

void DeterministicReactorRuntime::initialize() {
  std::stable_sort(impl_->reactors.begin(), impl_->reactors.end(), reactor_precedes);
  for (std::size_t i = 0; i < impl_->reactors.size(); ++i) {
    ScheduledReactor& reactor = impl_->reactors[i];
    if (!reactor.task.initialize) {
      continue;
    }
    ReactorContext context{i, 0, 0.0, &reactor.reactor_arena, &reactor.frame_arena};
    reactor.task.initialize(context);
  }
  impl_->initialized = true;
}

void DeterministicReactorRuntime::run_ticks(std::uint64_t count,
                                            const std::function<void(std::uint64_t, double)>& after_tick) {
  if (!impl_->initialized) {
    initialize();
  }
  for (std::uint64_t tick = 0; tick < count; ++tick) {
    impl_->jobs.clear();
    for (std::size_t i = 0; i < impl_->reactors.size(); ++i) {
      ScheduledReactor& reactor = impl_->reactors[i];
      reactor.frame_arena.reset();
      const double logical_time = reactor.task.period_seconds > 0.0
                                      ? static_cast<double>(tick) * reactor.task.period_seconds
                                      : static_cast<double>(tick);
      impl_->jobs.emplace_back([tick, logical_time, i, &reactor] {
        ReactorContext context{i, tick, logical_time, &reactor.reactor_arena, &reactor.frame_arena};
        reactor.task.tick(context);
      });
    }

    std::size_t begin = 0;
    while (begin < impl_->reactors.size()) {
      const bool parallel = impl_->reactors[begin].task.parallel_safe && impl_->workers != nullptr;
      std::size_t end = begin + 1;
      if (parallel) {
        while (end < impl_->reactors.size() && impl_->reactors[end].task.parallel_safe &&
               impl_->reactors[end].task.phase == impl_->reactors[begin].task.phase) {
          ++end;
        }
        impl_->workers->run(impl_->jobs, begin, end);
      } else {
        impl_->jobs[begin]();
      }
      begin = end;
    }

    for (std::size_t i = 0; i < impl_->reactors.size(); ++i) {
      ScheduledReactor& reactor = impl_->reactors[i];
      if (!reactor.task.commit) {
        continue;
      }
      const double logical_time = reactor.task.period_seconds > 0.0
                                      ? static_cast<double>(tick) * reactor.task.period_seconds
                                      : static_cast<double>(tick);
      ReactorContext context{i, tick, logical_time, &reactor.reactor_arena, &reactor.frame_arena};
      reactor.task.commit(context);
    }
    if (after_tick) {
      after_tick(tick, impl_->reactors.empty() || impl_->reactors.front().task.period_seconds <= 0.0
                           ? static_cast<double>(tick)
                           : static_cast<double>(tick) * impl_->reactors.front().task.period_seconds);
    }
  }
}

std::size_t DeterministicReactorRuntime::reactor_count() const {
  return impl_->reactors.size();
}

}  // namespace fluxion

extern "C" int fluxion_viz_cartpole(double stamp, double cart_x, double pole_theta) {
  CartPoleVisualizer& visualizer = cartpole_visualizer();
  std::lock_guard<std::mutex> guard(visualizer.lock);
  if (!visualizer.enabled) {
    return 0;
  }

  constexpr int width = 80;
  constexpr int height = 22;
  constexpr int track_y = 17;
  constexpr int pole_len = 10;
  constexpr double cart_limit = 2.4;
  constexpr double pi = 3.14159265358979323846;

  std::vector<std::string> canvas(height, std::string(width, ' '));
  canvas[0] = "Fluxion cartpole visualizer";
  std::ostringstream stats;
  stats << std::fixed << std::setprecision(3)
        << "t=" << stamp << "s  x=" << cart_x << "m  theta=" << pole_theta << "rad  shove=" << visualizer.last_shove;
  canvas[1].replace(0, std::min<int>(width, stats.str().size()), stats.str().substr(0, width));
  const std::string controls = "left/right arrows: shove cart   Ctrl-C: stop";
  canvas[2].replace(0, std::min<int>(width, controls.size()), controls.substr(0, width));

  for (int x = 2; x < width - 2; ++x) {
    canvas[track_y][x] = '-';
  }
  canvas[track_y][2] = '[';
  canvas[track_y][width - 3] = ']';

  const double normalized = (std::clamp(cart_x, -cart_limit, cart_limit) + cart_limit) / (cart_limit * 2.0);
  const int cart_col = 4 + static_cast<int>(std::lround(normalized * (width - 9)));
  const int cart_y = track_y - 1;
  for (int dx = -3; dx <= 3; ++dx) {
    const int x = cart_col + dx;
    if (x >= 0 && x < width) {
      canvas[cart_y][x] = dx == -3 || dx == 3 ? '|' : '=';
    }
  }
  canvas[cart_y + 1][std::max(0, cart_col - 2)] = 'o';
  canvas[cart_y + 1][std::min(width - 1, cart_col + 2)] = 'o';

  const double angle = pole_theta + pi;
  const int tip_x = cart_col + static_cast<int>(std::lround(std::sin(angle) * pole_len));
  const int tip_y = cart_y - static_cast<int>(std::lround(std::cos(angle) * pole_len));
  draw_line(canvas, cart_col, cart_y - 1, tip_x, tip_y, '*');
  if (cart_y - 1 >= 0) {
    canvas[cart_y - 1][cart_col] = '+';
  }

  std::cout << (visualizer.drew_frame ? "\033[H" : "\033[2J\033[H");
  visualizer.drew_frame = true;
  for (const auto& row : canvas) {
    std::cout << row << '\n';
  }
  std::cout.flush();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  return 0;
}

extern "C" double fluxion_viz_cartpole_shove(double magnitude) {
  CartPoleVisualizer& visualizer = cartpole_visualizer();
  std::lock_guard<std::mutex> guard(visualizer.lock);
  if (!visualizer.enabled) {
    return 0.0;
  }
  visualizer.last_shove = poll_cartpole_shove(std::abs(magnitude));
  return visualizer.last_shove;
}

extern "C" int fluxion_otel_span_start(const char* name) {
  const int span_id = otel_sink().next_span_id.fetch_add(1);
  write_otel_record("{\"schema\":\"fluxion.otel.v1\",\"event\":\"span_start\",\"span_id\":" +
                    std::to_string(span_id) + ",\"name\":\"" + json_escape(name) + "\"}");
  return span_id;
}

extern "C" int fluxion_otel_span_end(int span_id) {
  write_otel_record("{\"schema\":\"fluxion.otel.v1\",\"event\":\"span_end\",\"span_id\":" +
                    std::to_string(span_id) + "}");
  return 0;
}

extern "C" int fluxion_otel_event_i32(const char* name, int value) {
  write_otel_record("{\"schema\":\"fluxion.otel.v1\",\"event\":\"metric\",\"name\":\"" +
                    json_escape(name) + "\",\"type\":\"i32\",\"value\":" + std::to_string(value) + "}");
  return 0;
}

extern "C" int fluxion_otel_event_f64(const char* name, double value) {
  std::ostringstream payload;
  payload << "{\"schema\":\"fluxion.otel.v1\",\"event\":\"metric\",\"name\":\"" << json_escape(name)
          << "\",\"type\":\"f64\",\"value\":" << value << "}";
  write_otel_record(payload.str());
  return 0;
}
