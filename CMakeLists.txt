cmake_minimum_required(VERSION 3.14)
project(Regex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(regex src/regex.cc)
add_executable(naive_regex src/naive_regex.cc)
