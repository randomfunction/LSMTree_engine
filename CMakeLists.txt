cmake_minimum_required(VERSION 3.16)
project(lsmtree_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(lsmtree_demo
    src/main.cpp
    src/trace_logger.cpp
    src/bloom_filter.cpp
    src/memtable.cpp
    src/lsm_engine.cpp
    src/sstable.cpp
    src/wal.cpp
)

target_include_directories(lsmtree_demo PRIVATE include)

add_executable(lsmtree_small_test
    src/small_test.cpp
    src/trace_logger.cpp
    src/bloom_filter.cpp
    src/memtable.cpp
    src/lsm_engine.cpp
    src/sstable.cpp
    src/wal.cpp
)

target_include_directories(lsmtree_small_test PRIVATE include)
