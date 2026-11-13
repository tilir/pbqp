# Reproducible ThreadSanitizer checks with an equally instrumented oneTBB.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED PBQP_TBB_SOURCE OR NOT EXISTS "${PBQP_TBB_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR
    "Pass -DPBQP_TBB_SOURCE=/path/to/oneTBB before -P cmake/RunTSan.cmake")
endif()
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH PBQP_SOURCE)
if(NOT DEFINED PBQP_TSAN_ROOT)
  set(PBQP_TSAN_ROOT "${PBQP_SOURCE}/build-clang-modules-tsan")
endif()
if(NOT DEFINED PBQP_BUILD_JOBS)
  set(PBQP_BUILD_JOBS 4)
endif()
find_program(PBQP_C_COMPILER NAMES clang-21 REQUIRED)
find_program(PBQP_CXX_COMPILER NAMES clang++-21 REQUIRED)
find_program(PBQP_CTEST NAMES ctest REQUIRED)

function(run)
  execute_process(COMMAND ${ARGV} COMMAND_ECHO STDOUT RESULT_VARIABLE result)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "TSan check command failed: ${result}")
  endif()
endfunction()

set(tbb_build "${PBQP_TSAN_ROOT}/tbb-build")
set(tbb_install "${PBQP_TSAN_ROOT}/tbb-install")
set(pbqp_build "${PBQP_TSAN_ROOT}/pbqp-build")
run("${CMAKE_COMMAND}" -G Ninja -S "${PBQP_TBB_SOURCE}" -B "${tbb_build}"
  -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_COMPILER=${PBQP_CXX_COMPILER}"
  "-DCMAKE_INSTALL_PREFIX=${tbb_install}" -DCMAKE_INSTALL_LIBDIR=lib
  -DTBB_SANITIZE=thread -DTBB_TEST=OFF -DTBB_EXAMPLES=OFF
  -DTBBMALLOC_BUILD=OFF -DTBB_STRICT=OFF)
run("${CMAKE_COMMAND}" --build "${tbb_build}" --parallel "${PBQP_BUILD_JOBS}")
run("${CMAKE_COMMAND}" --install "${tbb_build}")
run("${CMAKE_COMMAND}" -G Ninja -S "${PBQP_SOURCE}" -B "${pbqp_build}"
  -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_C_COMPILER=${PBQP_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER=${PBQP_CXX_COMPILER}"
  "-DTBB_DIR=${tbb_install}/lib/cmake/TBB"
  "-DCMAKE_C_FLAGS=-fsanitize=thread -fno-omit-frame-pointer"
  "-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer"
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
  -DBUILD_TESTING=ON -DPBQP_BUILD_BENCHMARKS=OFF)
run("${CMAKE_COMMAND}" --build "${pbqp_build}"
  --target test_parallel_exact test_search_internals --parallel "${PBQP_BUILD_JOBS}")
run("${CMAKE_COMMAND}" -E env TSAN_OPTIONS=halt_on_error=1
  "${PBQP_CTEST}" --test-dir "${pbqp_build}" --output-on-failure
  -R "parallel_exact|search_internals")
