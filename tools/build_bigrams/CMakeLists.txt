# Tool offline di preprocessing bigrammi. Standalone (nessuna dipendenza dal
# core); include solo l'header del formato binario condiviso.
add_executable(build_bigrams build_bigrams.cpp)
target_include_directories(build_bigrams PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
