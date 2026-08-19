cmake_minimum_required(VERSION 3.16)
project(vibribot_mojib_tts LANGUAGES CXX)

add_executable(mojib_tts
    tts_cli.cpp
    mora.cpp
    r5900_float.cpp
    vrs.cpp
)

target_compile_features(mojib_tts PRIVATE cxx_std_17)

set_target_properties(mojib_tts PROPERTIES CXX_EXTENSIONS OFF)
