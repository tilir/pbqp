# Explicit example sources, targets, and executable smoke tests.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_C_EXAMPLE_SOURCES solve.c)
set(PBQP_CPP_EXAMPLE_SOURCES solve.cpp)
pbqp_register_format_sources(${PBQP_C_EXAMPLE_SOURCES} ${PBQP_CPP_EXAMPLE_SOURCES})
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)
if(NOT PBQP_BUILD_EXAMPLES)
  return()
endif()

add_executable(pbqp_example_c ${PBQP_C_EXAMPLE_SOURCES})
target_link_libraries(pbqp_example_c PRIVATE pbqp)
target_compile_features(pbqp_example_c PRIVATE c_std_99)
set_target_properties(pbqp_example_c PROPERTIES C_EXTENSIONS OFF)
pbqp_set_output_directories(pbqp_example_c)

add_executable(pbqp_example_cpp ${PBQP_CPP_EXAMPLE_SOURCES})
target_link_libraries(pbqp_example_cpp PRIVATE pbqp)
pbqp_configure_cxx_target(pbqp_example_cpp PRIVATE)

if(BUILD_TESTING)
  add_test(NAME example_c COMMAND pbqp_example_c)
  set_tests_properties(example_c PROPERTIES PASS_REGULAR_EXPRESSION "COST 1")
  add_test(NAME example_cpp COMMAND pbqp_example_cpp
    "${PROJECT_SOURCE_DIR}/examples/pbqp/01_basic.pbqp")
  set_tests_properties(example_cpp PROPERTIES PASS_REGULAR_EXPRESSION "COST 0")
  if(BUILD_SHARED_LIBS)
    find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
      add_test(NAME example_python COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${PROJECT_SOURCE_DIR}/bindings/python"
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/solve.py"
        "$<TARGET_FILE:pbqp>" "${PROJECT_SOURCE_DIR}/examples/pbqp/01_basic.pbqp")
      set_tests_properties(example_python PROPERTIES PASS_REGULAR_EXPRESSION "COST 0")
      add_test(NAME python_binding COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${PROJECT_SOURCE_DIR}/bindings/python"
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/test_python_binding.py"
        "$<TARGET_FILE:pbqp>" "${PROJECT_SOURCE_DIR}/examples/pbqp/01_basic.pbqp")
    endif()
    find_program(PBQP_RUBY_EXECUTABLE NAMES ruby)
    if(PBQP_RUBY_EXECUTABLE)
      add_test(NAME example_ruby COMMAND "${PBQP_RUBY_EXECUTABLE}"
        "-I${PROJECT_SOURCE_DIR}/bindings/ruby" "${CMAKE_CURRENT_SOURCE_DIR}/solve.rb"
        "$<TARGET_FILE:pbqp>" "${PROJECT_SOURCE_DIR}/examples/pbqp/01_basic.pbqp")
      set_tests_properties(example_ruby PROPERTIES PASS_REGULAR_EXPRESSION "COST 0")
      add_test(NAME ruby_binding COMMAND "${PBQP_RUBY_EXECUTABLE}"
        "-I${PROJECT_SOURCE_DIR}/bindings/ruby" "${PROJECT_SOURCE_DIR}/tests/test_ruby_binding.rb"
        "$<TARGET_FILE:pbqp>" "${PROJECT_SOURCE_DIR}/examples/pbqp/01_basic.pbqp")
    endif()
  endif()
endif()
