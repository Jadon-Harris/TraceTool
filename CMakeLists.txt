cmake_minimum_required(VERSION 3.16)

project(TraceTool LANGUAGES C)

option(ETE_TRBE_TARGET "Build real AArch64 ETE/TRBE target support" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(ete_trbe
    src/ete_trbe.c
)
target_include_directories(ete_trbe PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(ete_trbe PRIVATE
    $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

if(ETE_TRBE_TARGET)
    target_compile_definitions(ete_trbe PUBLIC ETE_TRBE_TARGET=1)
endif()

add_executable(ete_trace
    tools/ete_trace/ete_trace.c
)
target_link_libraries(ete_trace PRIVATE ete_trbe)
target_compile_options(ete_trace PRIVATE
    $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

enable_testing()

add_executable(test_metadata_json
    tests/unit/test_metadata_json.c
)
target_link_libraries(test_metadata_json PRIVATE ete_trbe)
add_test(NAME unit_metadata_json COMMAND test_metadata_json)
set_tests_properties(unit_metadata_json PROPERTIES LABELS "host")

add_executable(test_ring_buffer
    tests/unit/test_ring_buffer.c
)
target_link_libraries(test_ring_buffer PRIVATE ete_trbe)
add_test(NAME unit_ring_buffer COMMAND test_ring_buffer)
set_tests_properties(unit_ring_buffer PROPERTIES LABELS "host")

add_executable(test_mock_probe
    tests/mock/test_mock_probe.c
)
target_link_libraries(test_mock_probe PRIVATE ete_trbe)
add_test(NAME mock_probe COMMAND test_mock_probe)
set_tests_properties(mock_probe PROPERTIES LABELS "mock")

add_executable(test_sysreg_mock
    tests/mock/test_sysreg_mock.c
)
target_link_libraries(test_sysreg_mock PRIVATE ete_trbe)
target_include_directories(test_sysreg_mock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME mock_sysreg COMMAND test_sysreg_mock)
set_tests_properties(mock_sysreg PROPERTIES LABELS "mock")

add_executable(test_capture_state_mock
    tests/mock/test_capture_state_mock.c
)
target_link_libraries(test_capture_state_mock PRIVATE ete_trbe)
add_test(NAME mock_capture_state COMMAND test_capture_state_mock)
set_tests_properties(mock_capture_state PROPERTIES LABELS "mock")
