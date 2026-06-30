cmake_minimum_required(VERSION 3.16)
project(OneHand LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Il motore portabile: compila ovunque (Windows, macOS, Linux).
add_subdirectory(core)

# Il frontend nativo: per ora solo Windows.
if(WIN32)
    add_subdirectory(platform/windows)
endif()
