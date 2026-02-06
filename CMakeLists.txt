cmake_minimum_required(VERSION 3.15)
project(AudioCapture CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE)

# Add source files
add_executable(AudioCaptureCpp
    main.cpp
    audio_capture.h
    audio_capture.cpp
)

# Link required Windows libraries
target_link_libraries(AudioCaptureCpp PRIVATE
    ole32
    mmdevapi
    winmm
    propsys
)

# Set as Windows application (no console)
set_target_properties(AudioCaptureCpp PROPERTIES
    WIN32_EXECUTABLE ON
)
