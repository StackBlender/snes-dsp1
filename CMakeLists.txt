# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(dsp1 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(dsp1 STATIC src/dsp1/Dsp1.cpp)
target_include_directories(dsp1 PUBLIC src)

# Replays recorded questions and answers through the DSP-1 (see docs/checking.md).
add_executable(dsp1_check tools/dsp1_check.cpp)
target_link_libraries(dsp1_check PRIVATE dsp1)
