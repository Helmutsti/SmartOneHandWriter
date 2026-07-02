# Assistente di digitazione: applicazione Windows (FE) sopra il MOTORE.
add_executable(sohw_assistant WIN32 main.cpp)
target_link_libraries(sohw_assistant PRIVATE motore_core user32 gdi32)
target_compile_definitions(sohw_assistant PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN
    SOHW_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
