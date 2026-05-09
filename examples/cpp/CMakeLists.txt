cmake_minimum_required(VERSION 3.16)

project(fluxion_control_cpp_examples LANGUAGES CXX)

add_executable(cartpole_pid_mpc cartpole_pid_mpc.cpp)
target_compile_features(cartpole_pid_mpc PRIVATE cxx_std_17)

if (MSVC)
  target_compile_options(cartpole_pid_mpc PRIVATE /W4)
else()
  target_compile_options(cartpole_pid_mpc PRIVATE -Wall -Wextra -Wpedantic)
endif()
