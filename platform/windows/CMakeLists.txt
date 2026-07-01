# Frontend Windows: l'eseguibile onehand.exe.

add_executable(onehand WIN32 main_win32.cpp)
target_link_libraries(onehand PRIVATE onehand_core user32 gdi32)
target_compile_definitions(onehand PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

# Copia config.json e wordlist accanto all'eseguibile (devono stargli vicino).
add_custom_command(TARGET onehand POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/data/config.json      $<TARGET_FILE_DIR:onehand>/config.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/data/wordlist_it.txt  $<TARGET_FILE_DIR:onehand>/wordlist_it.txt
    VERBATIM
)
