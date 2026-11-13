# CTest coverage for the library, C ABI, and command-line tools.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

set(PBQP_TEST_SOURCES test_pbqp.cpp)
set(PBQP_PARALLEL_TEST_SOURCES test_parallel_exact.cpp)
set(PBQP_REDUCTION_TEST_SOURCES test_reduction.cpp)
set(PBQP_C_API_TEST_SOURCES test_c_api.c)
set(PBQP_INTERNAL_TEST_SOURCES test_search_internals.cpp)
set(PBQP_INTERNAL_TEST_SUPPORT search_test_support.cpp)
pbqp_register_format_sources(${PBQP_TEST_SOURCES} ${PBQP_REDUCTION_TEST_SOURCES}
  ${PBQP_C_API_TEST_SOURCES} ${PBQP_PARALLEL_TEST_SOURCES}
  ${PBQP_INTERNAL_TEST_SOURCES} ${PBQP_INTERNAL_TEST_SUPPORT})
set(PBQP_FORMAT_SOURCES "${PBQP_FORMAT_SOURCES}" PARENT_SCOPE)

if(NOT BUILD_TESTING)
  return()
endif()

add_executable(test_pbqp ${PBQP_TEST_SOURCES})
target_link_libraries(test_pbqp PRIVATE pbqp_generator)
pbqp_configure_cxx_target(test_pbqp PRIVATE)
add_test(NAME pbqp_tests COMMAND test_pbqp)
set_tests_properties(pbqp_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(test_parallel_exact ${PBQP_PARALLEL_TEST_SOURCES})
target_link_libraries(test_parallel_exact PRIVATE pbqp_generator)
pbqp_configure_cxx_target(test_parallel_exact PRIVATE)
add_test(NAME parallel_exact COMMAND test_parallel_exact)
set_tests_properties(parallel_exact PROPERTIES TIMEOUT 30)

add_executable(test_reduction ${PBQP_REDUCTION_TEST_SOURCES})
target_link_libraries(test_reduction PRIVATE pbqp)
pbqp_configure_cxx_target(test_reduction PRIVATE)
add_test(NAME reduction COMMAND test_reduction)

# Compile private-module test support in the owning target; clients cannot
# import private partitions. The bridge is absent with BUILD_TESTING=OFF.
target_sources(pbqp PRIVATE ${PBQP_INTERNAL_TEST_SUPPORT})
add_executable(test_search_internals ${PBQP_INTERNAL_TEST_SOURCES})
target_link_libraries(test_search_internals PRIVATE pbqp)
pbqp_configure_cxx_target(test_search_internals PRIVATE)
add_test(NAME search_internals COMMAND test_search_internals)

add_executable(test_c_api ${PBQP_C_API_TEST_SOURCES})
target_link_libraries(test_c_api PRIVATE pbqp)
target_compile_features(test_c_api PRIVATE c_std_99)
set_target_properties(test_c_api PROPERTIES C_EXTENSIONS OFF)
pbqp_set_output_directories(test_c_api)
add_test(NAME c_api COMMAND test_c_api)

add_test(NAME cli COMMAND "${CMAKE_COMMAND}"
  "-DPBQP=$<TARGET_FILE:pbqp_cli>"
  "-DGENERATOR=$<TARGET_FILE:pbqp_generate>"
  "-DEXAMPLE=${PROJECT_SOURCE_DIR}/examples/01_basic.pbqp"
  "-DHEURISTIC_EXAMPLE=${PROJECT_SOURCE_DIR}/examples/06_too_long_for_any_exact.pbqp"
  "-DWORK=${PROJECT_BINARY_DIR}/cli-test"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake")
