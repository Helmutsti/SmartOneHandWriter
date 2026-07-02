# Banco di prova Win32 del CORE (solo Windows, zero dipendenze esterne).
add_executable(sohw_gui_win32 WIN32 main.cpp)
target_link_libraries(sohw_gui_win32 PRIVATE sohw_core user32 gdi32)
target_compile_definitions(sohw_gui_win32 PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN
    SOHW_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
