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
# Contiene CORE (sohw) + MOTORE (motore) + libreria di base (onehand_core).
add_subdirectory(core)

# Il frontend nativo: l'assistente di digitazione (FE sopra il MOTORE).
if(WIN32)
    # Windows: Win32/GDI, linkato direttamente a motore_core (C++).
    add_subdirectory(app/windows)
elseif(APPLE)
    # macOS: frontend Swift/AppKit. Non si compila da CMake (serve swiftc): lo
    # costruisce app/macos/build_macos.sh, che linka il target motore_c (la C ABI
    # del MOTORE, sempre inclusa in core/) e assembla il bundle .app.
    message(STATUS "macOS: costruisci il frontend con app/macos/build_macos.sh (usa il target motore_c)")
endif()
