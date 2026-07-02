# Banco di prova Qt del CORE. Opt-in: se Qt6 non e' disponibile, si salta senza
# errori (il resto del progetto compila comunque).
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 non trovato: target sohw_gui saltato (usa CMAKE_PREFIX_PATH per abilitarlo).")
    return()
endif()

set(CMAKE_AUTOMOC ON)

add_executable(sohw_gui WIN32 main.cpp)
target_link_libraries(sohw_gui PRIVATE sohw_core Qt6::Widgets)
target_compile_definitions(sohw_gui PRIVATE SOHW_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
