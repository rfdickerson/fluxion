#pragma once

extern "C" int fluxion_print_f64(double value);
extern "C" int fluxion_print_i32(int value);
extern "C" int fluxion_print_bool(bool value);
extern "C" int fluxion_print_string(const char* value);
extern "C" int fluxion_print_matrix(void* matrix);
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

}  // namespace fluxion
