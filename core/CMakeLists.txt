# Il cuore portabile di OneHand: nessuna dipendenza dal sistema operativo.

# --- Libreria statica: usata direttamente dal frontend C++ -------------------
add_library(onehand_core STATIC
    src/engine.cpp
    src/dictionary.cpp
    src/config.cpp
    src/utf8.cpp
    src/alterations.cpp
    src/predictor_frequency.cpp
)
target_include_directories(onehand_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --- C ABI opzionale: per frontend in altri linguaggi (Swift/C#/Rust) --------
# Non serve al frontend C++. Attivala con -DONEHAND_BUILD_C_ABI=ON.
option(ONEHAND_BUILD_C_ABI "Build the optional C ABI shared library (onehand_c)" OFF)
if(ONEHAND_BUILD_C_ABI)
    add_library(onehand_c SHARED src/onehand_c.cpp)
    target_link_libraries(onehand_c PRIVATE onehand_core)
    target_include_directories(onehand_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

# --- Test del motore (senza GUI/OS) ------------------------------------------
option(ONEHAND_BUILD_TESTS "Build core unit tests" ON)
if(ONEHAND_BUILD_TESTS)
    enable_testing()
    add_executable(engine_tests tests/engine_tests.cpp)
    target_link_libraries(engine_tests PRIVATE onehand_core)
    add_test(NAME engine_tests COMMAND engine_tests)
endif()
