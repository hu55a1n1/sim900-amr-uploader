cmake_minimum_required(VERSION 3.0)
project(sim900-amr-uploader)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")

set(SOURCE_FILES main.c)
add_executable(sim900-amr-uploader ${SOURCE_FILES})
target_link_libraries(sim900-amr-uploader serialport udev)
