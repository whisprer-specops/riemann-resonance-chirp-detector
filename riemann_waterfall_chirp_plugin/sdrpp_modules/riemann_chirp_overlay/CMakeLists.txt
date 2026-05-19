cmake_minimum_required(VERSION 3.13)
project(riemann_chirp_overlay)

file(GLOB SRC "src/*.cpp")
include(${SDRPP_MODULE_CMAKE})

target_include_directories(riemann_chirp_overlay PRIVATE "src/")
target_compile_features(riemann_chirp_overlay PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(riemann_chirp_overlay PRIVATE /permissive- /W3)
else()
    target_compile_options(riemann_chirp_overlay PRIVATE -Wall -Wextra -Wpedantic)
endif()
