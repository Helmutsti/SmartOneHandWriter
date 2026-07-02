# Banco di prova da terminale del CORE. Zero dipendenze (solo sohw_core).
add_executable(sohw_cli main.cpp)
target_link_libraries(sohw_cli PRIVATE sohw_core)
