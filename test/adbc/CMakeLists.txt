cmake_minimum_required(VERSION 3.5)
project(repro CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DUCKLAKE_SRC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../)

add_executable(repro repro.cpp)
#target_compile_options(repro PRIVATE -O3 -DNDEBUG)
target_compile_options(repro PRIVATE -O2 -g -fsanitize=address)
target_link_options(repro PRIVATE -fsanitize=address)
target_include_directories(repro PRIVATE ${DUCKLAKE_SRC_ROOT}/duckdb/src/include/)
target_link_libraries(repro PRIVATE ${DUCKLAKE_SRC_ROOT}/build/relassert/src/libduckdb.dylib)

add_executable(repro1 repro1.cpp)
target_compile_options(repro1 PRIVATE -O2 -g -fsanitize=address)
target_link_options(repro1 PRIVATE -fsanitize=address)
target_include_directories(repro1 PRIVATE ${DUCKLAKE_SRC_ROOT}/duckdb/src/include/)
target_link_libraries(repro1 PRIVATE ${DUCKLAKE_SRC_ROOT}/build/relassert/src/libduckdb.dylib)
