cmake_minimum_required(VERSION 3.2)
project(GGC)

include_directories(include)

add_executable(GGC main.c gc.h gc.c)