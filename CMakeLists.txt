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

# Tool offline (preprocessing bigrammi). Attivo di default; nessuna GUI.
option(SOHW_BUILD_TOOLS "Build offline tools (build_bigrams)" ON)
if(SOHW_BUILD_TOOLS)
    add_subdirectory(tools/build_bigrams)
endif()

# Il frontend nativo: per ora solo Windows.
if(WIN32)
    add_subdirectory(platform/windows)
    add_subdirectory(gui/win32)   # banco di prova Win32 del CORE
endif()

# Banco di prova da terminale del CORE (sempre disponibile, zero dipendenze).
add_subdirectory(cli)

# Banco di prova Qt del CORE "nuova concezione" (opt-in, si salta senza Qt6).
option(SOHW_BUILD_GUI "Build the Qt test GUI for the CORE" ON)
if(SOHW_BUILD_GUI)
    add_subdirectory(gui/qt)
endif()
