cmake_minimum_required(VERSION 3.16)
project(OneHand LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# I sorgenti sono UTF-8 senza BOM (usano lettere accentate e simboli Unicode).
# Senza questo flag MSVC li legge con la code page ANSI del sistema e li storpia.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Il motore portabile: compila ovunque (Windows, macOS, Linux).
add_subdirectory(core)

# Il frontend nativo: per ora solo Windows.
if(WIN32)
    add_subdirectory(platform/windows)
endif()
