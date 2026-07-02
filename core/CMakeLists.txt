# Il cuore portabile di OneHand: nessuna dipendenza dal sistema operativo.

# --- Libreria statica: usata direttamente dal frontend C++ -------------------
add_library(onehand_core STATIC
    src/engine.cpp
    src/dictionary.cpp
    src/config.cpp
    src/utf8.cpp
    src/alterations.cpp
    src/predictor_frequency.cpp
    src/ngram_predictor.cpp
)
target_include_directories(onehand_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --- CORE "nuova concezione" (sohw): facade stateless matching+predittivo ----
# Riusa onehand_core (utf8, dictionary, config, alterations, predictor...).
add_library(sohw_core STATIC
    src/sohw/core.cpp
    src/sohw/candidate_provider.cpp
    src/sohw/bigram_model.cpp
    src/sohw/bigram_predictor.cpp
)
target_include_directories(sohw_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(sohw_core PUBLIC onehand_core)

# --- C ABI opzionale: per frontend in altri linguaggi (Swift/C#/Rust) --------
# Non serve al frontend C++. Attivala con -DONEHAND_BUILD_C_ABI=ON.
option(ONEHAND_BUILD_C_ABI "Build the optional C ABI shared library (onehand_c)" OFF)
if(ONEHAND_BUILD_C_ABI)
    add_library(onehand_c SHARED src/onehand_c.cpp)
    target_link_libraries(onehand_c PRIVATE onehand_core)
    target_include_directories(onehand_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

# --- C ABI del CORE "nuova concezione" (smartcore_c), opt-in shared ----------
option(SOHW_BUILD_C_ABI "Build the CORE C ABI shared library (smartcore_c)" OFF)
if(SOHW_BUILD_C_ABI)
    add_library(smartcore_c SHARED src/sohw/smartcore_c.cpp)
    target_link_libraries(smartcore_c PRIVATE sohw_core)
    target_include_directories(smartcore_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # Su MSVC esporta automaticamente i simboli extern "C" senza annotazioni.
    set_target_properties(smartcore_c PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# --- Test del motore (senza GUI/OS) ------------------------------------------
option(ONEHAND_BUILD_TESTS "Build core unit tests" ON)
if(ONEHAND_BUILD_TESTS)
    enable_testing()
    add_executable(engine_tests tests/engine_tests.cpp)
    target_link_libraries(engine_tests PRIVATE onehand_core)
    add_test(NAME engine_tests COMMAND engine_tests)

    add_executable(sohw_tests tests/sohw_tests.cpp)
    target_link_libraries(sohw_tests PRIVATE sohw_core)
    target_compile_definitions(sohw_tests PRIVATE SOHW_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
    add_test(NAME sohw_tests COMMAND sohw_tests)

    # Test del Dictionary: include header interno (core/src).
    add_executable(dict_tests tests/dict_tests.cpp)
    target_link_libraries(dict_tests PRIVATE onehand_core)
    target_include_directories(dict_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dict_tests COMMAND dict_tests)

    add_executable(provider_tests tests/provider_tests.cpp)
    target_link_libraries(provider_tests PRIVATE sohw_core)
    add_test(NAME provider_tests COMMAND provider_tests)

    add_executable(bigram_tests tests/bigram_tests.cpp)
    target_link_libraries(bigram_tests PRIVATE sohw_core)
    # Se il modello reale e' stato generato, abilita la parte di integrazione.
    if(EXISTS ${CMAKE_SOURCE_DIR}/data/it.bigrams.bin)
        target_compile_definitions(bigram_tests PRIVATE
            SOHW_BIGRAM_BIN="${CMAKE_SOURCE_DIR}/data/it.bigrams.bin")
    endif()
    add_test(NAME bigram_tests COMMAND bigram_tests)

    # Mini-driver del C ABI: compila smartcore_c.cpp nel test e linka sohw_core.
    add_executable(abi_tests tests/abi_tests.cpp src/sohw/smartcore_c.cpp)
    target_link_libraries(abi_tests PRIVATE sohw_core)
    target_include_directories(abi_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME abi_tests COMMAND abi_tests)
endif()
