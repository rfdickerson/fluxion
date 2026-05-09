cmake_minimum_required(VERSION 3.20)

project(fluxion LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVMConfig.cmake: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})

add_executable(fluxion
  src/ast.cpp
  src/codegen_llvm.cpp
  src/jit.cpp
  src/lexer.cpp
  src/main.cpp
  src/parser.cpp
  src/physics_check.cpp
  src/runtime.cpp
  src/typecheck.cpp
)

target_include_directories(fluxion SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

llvm_map_components_to_libnames(LLVM_LIBS
  Core
  ExecutionEngine
  OrcJIT
  OrcTargetProcess
  Support
  Target
  native
)

target_link_libraries(fluxion PRIVATE ${LLVM_LIBS})

if (MSVC)
  target_compile_options(fluxion PRIVATE /W4 /EHsc)
else()
  target_compile_options(fluxion PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_test(NAME check_arithmetic COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/arithmetic.flx)
add_test(NAME check_records COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/records.flx)
add_test(NAME check_loop COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/bounded_loop.flx)
add_test(NAME check_opentelemetry COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/opentelemetry.flx)
add_test(NAME check_swift_erlang_surface COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/swift_erlang_surface.flx)
add_test(NAME check_matrices COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/matrices.flx)
add_test(NAME check_matrix_dimensions_and_units COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/matrix_dimensions_and_units.flx)
add_test(NAME check_core_reactor COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/valid/core_reactor.flx)
add_test(NAME physics_check_kalman COMMAND fluxion physics-check ${CMAKE_SOURCE_DIR}/examples/kalman_object_tracking.flx)
add_test(NAME physics_check_units COMMAND fluxion physics-check ${CMAKE_SOURCE_DIR}/tests/valid/matrix_dimensions_and_units.flx)
add_test(NAME physics_check_lints COMMAND fluxion physics-check ${CMAKE_SOURCE_DIR}/tests/valid/physics_lints.flx)
add_test(NAME run_arithmetic COMMAND fluxion run ${CMAKE_SOURCE_DIR}/tests/valid/arithmetic.flx)
add_test(NAME run_swift_erlang_surface COMMAND fluxion run ${CMAKE_SOURCE_DIR}/tests/valid/swift_erlang_surface.flx)
add_test(NAME run_matrices COMMAND fluxion run ${CMAKE_SOURCE_DIR}/tests/valid/matrices.flx)
add_test(NAME run_opentelemetry COMMAND fluxion run ${CMAKE_SOURCE_DIR}/tests/valid/opentelemetry.flx)
add_test(NAME run_cartpole COMMAND fluxion run ${CMAKE_SOURCE_DIR}/examples/runnable/cartpole_pid.flx)
add_test(NAME run_kalman_object_tracking COMMAND fluxion run ${CMAKE_SOURCE_DIR}/examples/kalman_object_tracking.flx)
add_test(NAME run_runnable_kalman_object_tracking COMMAND fluxion run ${CMAKE_SOURCE_DIR}/examples/runnable/kalman_object_tracking.flx)
add_test(NAME run_cartpole_pid_mpc COMMAND fluxion run ${CMAKE_SOURCE_DIR}/examples/cartpole_pid_mpc.flx)
add_test(NAME run_cartpole_visualizer COMMAND fluxion run --visualize ${CMAKE_SOURCE_DIR}/tests/valid/cartpole_visualizer.flx)
add_test(NAME repl_basic COMMAND fluxion repl ${CMAKE_SOURCE_DIR}/tests/repl/basic.repl)
add_test(NAME repl_matrix COMMAND fluxion repl ${CMAKE_SOURCE_DIR}/tests/repl/matrix.repl)
add_test(NAME emit_llvm COMMAND fluxion emit-llvm ${CMAKE_SOURCE_DIR}/tests/valid/arithmetic.flx)
add_test(NAME emit_llvm_opentelemetry COMMAND fluxion emit-llvm ${CMAKE_SOURCE_DIR}/tests/valid/opentelemetry.flx)
add_test(NAME reject_opentelemetry_name_type COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/opentelemetry_name_type.flx)
add_test(NAME reject_type_error COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/type_error.flx)
add_test(NAME reject_matrix_dimension_error COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/matrix_dimension_error.flx)
add_test(NAME reject_unit_mismatch COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/unit_mismatch.flx)
add_test(NAME reject_rust_style_syntax COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/rust_style_syntax.flx)
add_test(NAME reject_ragged_matrix COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/ragged_matrix.flx)
add_test(NAME reject_core_unknown_phase COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/core_unknown_phase.flx)
add_test(NAME reject_core_missing_capacity COMMAND fluxion check ${CMAKE_SOURCE_DIR}/tests/invalid/core_missing_capacity.flx)
set_tests_properties(run_arithmetic run_cartpole run_cartpole_pid_mpc PROPERTIES WILL_FAIL FALSE)
set_tests_properties(run_matrices PROPERTIES PASS_REGULAR_EXPRESSION "\\[6 8;")
set_tests_properties(run_kalman_object_tracking PROPERTIES PASS_REGULAR_EXPRESSION "estimated_state")
set_tests_properties(run_runnable_kalman_object_tracking PROPERTIES PASS_REGULAR_EXPRESSION "estimated_state")
set_tests_properties(physics_check_kalman PROPERTIES PASS_REGULAR_EXPRESSION "FXN-COV-003")
set_tests_properties(physics_check_units PROPERTIES PASS_REGULAR_EXPRESSION "FXN-UNIT-001")
set_tests_properties(physics_check_lints PROPERTIES PASS_REGULAR_EXPRESSION "FXN-NUM-021.*FXN-STOCH-014")
set_tests_properties(run_cartpole_pid_mpc PROPERTIES PASS_REGULAR_EXPRESSION "200")
set_tests_properties(run_cartpole_visualizer PROPERTIES PASS_REGULAR_EXPRESSION "Fluxion cartpole visualizer")
set_tests_properties(run_opentelemetry PROPERTIES
  ENVIRONMENT FLUXION_OTEL=stdout
  PASS_REGULAR_EXPRESSION "span_start"
)
set_tests_properties(repl_basic PROPERTIES PASS_REGULAR_EXPRESSION "3\nok\n42\ntrue\nhello fluxion")
set_tests_properties(repl_matrix PROPERTIES PASS_REGULAR_EXPRESSION "\\[1 2;")
set_tests_properties(emit_llvm_opentelemetry PROPERTIES PASS_REGULAR_EXPRESSION "fluxion_otel_span_start")
set_tests_properties(reject_opentelemetry_name_type reject_type_error reject_matrix_dimension_error reject_unit_mismatch reject_rust_style_syntax reject_ragged_matrix reject_core_unknown_phase reject_core_missing_capacity PROPERTIES WILL_FAIL TRUE)
