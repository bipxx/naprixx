cmake_minimum_required(VERSION 3.19)
project(naparixx_frontend_experiment LANGUAGES CXX)

find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
if(Python_VERSION VERSION_GREATER_EQUAL 3.14)
  message(FATAL_ERROR "This experimental private-frame code supports CPython 3.11-3.13 only")
endif()
execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_VARIABLE nanobind_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
find_package(nanobind CONFIG REQUIRED)
nanobind_add_module(_frontend_policy_probe NB_STATIC native_probe.cpp)
target_compile_features(_frontend_policy_probe PRIVATE cxx_std_17)
target_include_directories(_frontend_policy_probe PRIVATE ../../cpp/include)
if(MSVC)
  target_compile_options(_frontend_policy_probe PRIVATE /bigobj /utf-8)
endif()
# Deliberately no install target or connection to the production extension.
