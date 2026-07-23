cmake_minimum_required(VERSION 3.14)
project(Regex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(regex-0 src/regex-0.cc)
add_executable(regex-1 src/regex-1.cc)
add_executable(regex-2 src/regex-2.cc)
