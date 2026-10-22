cmake_minimum_required(VERSION 3.28)
project(saturn-sndlib CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT TARGET ymir-core)
    set(_ymir_default "${CMAKE_CURRENT_SOURCE_DIR}/../ymir")
    set(YMIR_DIR "${_ymir_default}" CACHE PATH
        "Path to a Ymir source checkout")
    if (NOT EXISTS "${YMIR_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Ymir was not found at '${YMIR_DIR}'. Pass "
            "-DYMIR_DIR=/path/to/Ymir or place it beside saturn-sndlib.")
    endif ()

    set(Ymir_LIBRARY_ONLY ON CACHE BOOL "" FORCE)
    set(Ymir_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(Ymir_ENABLE_SANDBOX OFF CACHE BOOL "" FORCE)
    set(Ymir_ENABLE_YMDASM OFF CACHE BOOL "" FORCE)
    set(Ymir_ENABLE_IPO OFF CACHE BOOL "" FORCE)
    add_subdirectory("${YMIR_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ymir")
endif ()

add_library(saturn-sndlib STATIC
    src/hle_sequence_player.cpp
    src/package.cpp
    src/sequence.cpp
    src/sound_system.cpp
    src/tone_bank.cpp
)
add_library(SaturnSndlib::Sndlib ALIAS saturn-sndlib)
target_include_directories(saturn-sndlib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(saturn-sndlib PUBLIC ymir-core)

option(SATURN_SNDLIB_BUILD_TESTS
    "Build saturn-sndlib self-contained tests" ${PROJECT_IS_TOP_LEVEL})
set(SATURN_SNDLIB_SOTN_GAME_DIR "" CACHE PATH
    "Optional extracted SOTN directory containing 0.BIN and SD")
if (SATURN_SNDLIB_BUILD_TESTS)
    enable_testing()

    add_executable(saturn-sndlib-mozart-smoke tests/mozart_smoke.cpp)
    target_link_libraries(saturn-sndlib-mozart-smoke PRIVATE
        SaturnSndlib::Sndlib)
    add_test(NAME saturn-sndlib-mozart-smoke
        COMMAND saturn-sndlib-mozart-smoke)

    add_executable(driver-version-test tests/driver_version_test.cpp)
    target_link_libraries(driver-version-test PRIVATE SaturnSndlib::Sndlib)
    add_test(NAME driver-version COMMAND driver-version-test)

    if (EXISTS "${SATURN_SNDLIB_SOTN_GAME_DIR}/0.BIN" AND
        EXISTS "${SATURN_SNDLIB_SOTN_GAME_DIR}/SD/SD_JO10.CRT" AND
        EXISTS "${SATURN_SNDLIB_SOTN_GAME_DIR}/SD/SD_ROU.CRT")
        function(add_saturn_sndlib_corpus_test target source test_name)
            add_executable("${target}" "${source}")
            target_compile_definitions("${target}" PRIVATE
                SATURN_SNDLIB_SOTN_GAME_DIR="${SATURN_SNDLIB_SOTN_GAME_DIR}")
            target_link_libraries("${target}" PRIVATE SaturnSndlib::Sndlib)
            add_test(NAME "${test_name}" COMMAND "${target}")
        endfunction ()

        add_saturn_sndlib_corpus_test(
            sequence-decoder-test tests/sequence_decoder_test.cpp
            sequence-decoder)
        add_saturn_sndlib_corpus_test(
            sequence-corpus-test tests/sequence_corpus_test.cpp
            sequence-corpus)
        add_saturn_sndlib_corpus_test(
            tone-corpus-test tests/tone_corpus_test.cpp tone-corpus)
        add_saturn_sndlib_corpus_test(
            sequence-lle-test tests/sequence_lle_test.cpp
            sequence-lle-oracle)
        add_saturn_sndlib_corpus_test(
            tone-lle-test tests/tone_lle_test.cpp tone-layer-lle-oracle)
        add_saturn_sndlib_corpus_test(
            hle-pcm-lle-test tests/hle_pcm_lle_test.cpp
            hle-pcm-lle-oracle)
    else ()
        message(STATUS
            "saturn-sndlib: Saturn corpus tests disabled; set "
            "SATURN_SNDLIB_SOTN_GAME_DIR to an extracted SOTN directory")
    endif ()
endif ()
