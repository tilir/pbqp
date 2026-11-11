cmake_minimum_required(VERSION 3.28)
project(pbqp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(cmake/Formatting.cmake)

add_library(pbqp src/problem_api.cpp src/io.cpp src/bruteforce.cpp)
target_sources(pbqp PRIVATE FILE_SET CXX_MODULES FILES
    src/pbqp.cppm src/error.cppm src/failure.cppm src/ownership.cppm
    src/problem.cppm src/text_reader.cppm)
target_include_directories(pbqp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(pbqp PRIVATE cxx_std_23)
set_target_properties(pbqp PROPERTIES CXX_EXTENSIONS OFF)

add_library(pbqp_command_line STATIC)
target_sources(pbqp_command_line PUBLIC FILE_SET CXX_MODULES FILES tools/command_line.cppm)
target_compile_features(pbqp_command_line PUBLIC cxx_std_23)
set_target_properties(pbqp_command_line PROPERTIES CXX_EXTENSIONS OFF)

add_executable(pbqp_cli tools/pbqp.cpp)
target_link_libraries(pbqp_cli PRIVATE pbqp pbqp_command_line)
target_compile_features(pbqp_cli PRIVATE cxx_std_23)
set_target_properties(pbqp_cli PROPERTIES OUTPUT_NAME pbqp CXX_EXTENSIONS OFF)

add_library(pbqp_generator STATIC)
target_sources(pbqp_generator PUBLIC FILE_SET CXX_MODULES FILES tools/generator.cppm)
target_link_libraries(pbqp_generator PUBLIC pbqp)
target_compile_features(pbqp_generator PUBLIC cxx_std_23)
set_target_properties(pbqp_generator PROPERTIES CXX_EXTENSIONS OFF)

add_executable(pbqp_generate tools/generate.cpp)
target_link_libraries(pbqp_generate PRIVATE pbqp_generator pbqp_command_line)
target_compile_features(pbqp_generate PRIVATE cxx_std_23)
set_target_properties(pbqp_generate PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target pbqp pbqp_cli pbqp_generate pbqp_generator pbqp_command_line)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_pbqp tests/test_pbqp.cpp)
    target_link_libraries(test_pbqp PRIVATE pbqp_generator)
    target_compile_features(test_pbqp PRIVATE cxx_std_23)
    set_target_properties(test_pbqp PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME pbqp_tests COMMAND test_pbqp)
    set_tests_properties(pbqp_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(test_c_api tests/test_c_api.c)
    target_link_libraries(test_c_api PRIVATE pbqp)
    target_compile_features(test_c_api PRIVATE c_std_99)
    set_target_properties(test_c_api PROPERTIES C_EXTENSIONS OFF)
    add_test(NAME c_api COMMAND test_c_api)

    add_test(NAME cli COMMAND "${CMAKE_COMMAND}"
        "-DPBQP=$<TARGET_FILE:pbqp_cli>"
        "-DGENERATOR=$<TARGET_FILE:pbqp_generate>"
        "-DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/1.pbqp"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
endif()
