cmake_minimum_required(VERSION 3.5)
project(os2016)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -O0")

add_executable(simplesh midtermsh/midtermsh.cpp)
add_executable(sigusr sigusr/sigusr.c)
add_executable(rshd rshd/rshd.cpp)
add_executable(test1 test1.cpp)
