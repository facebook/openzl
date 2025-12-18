project(test)
cmake_minimum_required(VERSION 3.20.2 FATAL_ERROR)

find_package(zstd REQUIRED)
find_package(lz4 REQUIRED)
find_package(OpenZL CONFIG REQUIRED)

add_executable(build_test main.cpp)
target_link_libraries(build_test OpenZL::openzl_cpp)
