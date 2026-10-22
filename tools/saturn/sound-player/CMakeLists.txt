cmake_minimum_required(VERSION 3.28)
project(saturn-sound-player CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(_bundled_saturn_sndlib "${CMAKE_CURRENT_SOURCE_DIR}/saturn-sndlib")
set(SATURN_SNDLIB_DIR "${_bundled_saturn_sndlib}"
    CACHE PATH "Path to a saturn-sndlib source checkout")
if (NOT EXISTS "${SATURN_SNDLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "saturn-sndlib was not found at '${SATURN_SNDLIB_DIR}'. Pass "
        "-DSATURN_SNDLIB_DIR=/path/to/saturn-sndlib or initialize the "
        "saturn_sound_player/saturn-sndlib submodule.")
endif ()

set(SOTN_GAME_DIR "" CACHE PATH
    "Optional extracted SOTN directory containing 0.BIN and SD")
set(SATURN_SNDLIB_BUILD_TESTS "${BUILD_TESTING}" CACHE BOOL
    "Build saturn-sndlib tests")
set(SATURN_SNDLIB_SOTN_GAME_DIR "${SOTN_GAME_DIR}" CACHE PATH
    "Extracted SOTN directory for saturn-sndlib corpus tests" FORCE)
add_subdirectory("${SATURN_SNDLIB_DIR}" "${CMAKE_BINARY_DIR}/saturn-sndlib")

add_library(saturn-player-core STATIC
    src/core/wav.cpp
)
add_library(SaturnPlayer::Core ALIAS saturn-player-core)
target_include_directories(saturn-player-core PUBLIC src/core)
target_link_libraries(saturn-player-core PUBLIC SaturnSndlib::Sndlib)

add_library(saturn-player-sotn STATIC
    src/games/sotn/sotn_data.cpp
    src/games/sotn/stage_sfx.cpp
)
add_library(SaturnPlayer::Sotn ALIAS saturn-player-sotn)
target_include_directories(saturn-player-sotn PUBLIC src/games/sotn)
target_link_libraries(saturn-player-sotn PUBLIC
    SaturnPlayer::Core
    SaturnSndlib::Sndlib
)

add_library(saturn-player-scorcher STATIC
    src/games/scorcher/scorcher_hle.cpp
    src/games/scorcher/scorcher_lle.cpp
    src/games/scorcher/scorcher_mus.cpp
)
add_library(SaturnPlayer::Scorcher ALIAS saturn-player-scorcher)
target_include_directories(saturn-player-scorcher PUBLIC src/games/scorcher)
target_link_libraries(saturn-player-scorcher PUBLIC
    SaturnPlayer::Core
    SaturnSndlib::Sndlib
)

find_package(SDL3 CONFIG QUIET)
if (SDL3_FOUND)
    target_sources(saturn-player-sotn PRIVATE
        src/games/sotn/sdl_player.cpp)
    target_link_libraries(saturn-player-sotn PRIVATE SDL3::SDL3)
    message(STATUS "saturn-sound-player: SDL3 player enabled")
else ()
    message(STATUS
        "saturn-sound-player: SDL3 not found; interactive player disabled")
endif ()

option(SATURN_PLAYER_BUILD_GAME_TESTS
    "Build game-adapter tests and optional retail-data oracles" ${BUILD_TESTING})
if (BUILD_TESTING AND SATURN_PLAYER_BUILD_GAME_TESTS)
    add_executable(scorcher-lle-hle-test
        tests/scorcher/scorcher_lle_hle_test.cpp)
    target_link_libraries(scorcher-lle-hle-test PRIVATE
        SaturnPlayer::Scorcher)

    set(SCORCHER_MUS_DIR "" CACHE PATH
        "Optional extracted SCORSAT/GAME/SATMUSIC directory for oracle tests")
    set(_scorcher_mus_complete TRUE)
    foreach (_name LEVEL1 LEVEL2 LEVEL3 LEVEL4 LEVEL5 LEVEL6 TITLE)
        if (EXISTS "${SCORCHER_MUS_DIR}/${_name}.MUS")
        else ()
            set(_scorcher_mus_complete FALSE)
        endif ()
    endforeach ()
    if (_scorcher_mus_complete)
        add_test(NAME scorcher-lle-hle-oracle
            COMMAND scorcher-lle-hle-test
                "${SCORCHER_MUS_DIR}/LEVEL1.MUS"
                "${SCORCHER_MUS_DIR}/LEVEL2.MUS"
                "${SCORCHER_MUS_DIR}/LEVEL3.MUS"
                "${SCORCHER_MUS_DIR}/LEVEL4.MUS"
                "${SCORCHER_MUS_DIR}/LEVEL5.MUS"
                "${SCORCHER_MUS_DIR}/LEVEL6.MUS"
                "${SCORCHER_MUS_DIR}/TITLE.MUS")
        set_tests_properties(scorcher-lle-hle-oracle
            PROPERTIES TIMEOUT 300)
    endif ()

    add_executable(scorcher-hle-test
        tests/scorcher/scorcher_hle_test.cpp)
    target_link_libraries(scorcher-hle-test PRIVATE
        SaturnPlayer::Scorcher)
    add_test(NAME scorcher-hle-smoke COMMAND scorcher-hle-test)

    add_executable(scorcher-mus-test
        tests/scorcher/scorcher_mus_test.cpp)
    target_link_libraries(scorcher-mus-test PRIVATE
        SaturnPlayer::Scorcher)
    add_test(NAME scorcher-mus-parse COMMAND scorcher-mus-test)

    add_executable(stage-sfx-test tests/sotn/stage_sfx_test.cpp)
    target_link_libraries(stage-sfx-test PRIVATE SaturnPlayer::Sotn)
    add_test(NAME stage-sfx-scan COMMAND stage-sfx-test)

    set(_sotn_corpus_complete TRUE)
    foreach (_path
            0.BIN
            SD/SD_JO10.CRT
            SD/SD_ALK.CRT
            SD/SD_LIH.CRT
            SD/SD_MARI.CRT
            SD/SD_ROU.CRT
            SD/SD_GOLD.CRT
            STAGE_02.PRG)
        if (NOT EXISTS "${SOTN_GAME_DIR}/${_path}")
            set(_sotn_corpus_complete FALSE)
        endif ()
    endforeach ()
    if (_sotn_corpus_complete)
        add_executable(sound-package-roundtrip-test
            tests/sotn/sound_package_roundtrip_test.cpp)
        target_compile_definitions(sound-package-roundtrip-test PRIVATE
            SOTN_GAME_DIR="${SOTN_GAME_DIR}")
        target_link_libraries(sound-package-roundtrip-test PRIVATE
            SaturnSndlib::Sndlib
            SaturnPlayer::Sotn
        )
        add_test(
            NAME sound-package-roundtrip
            COMMAND sound-package-roundtrip-test)

        add_executable(overlay-pcm-lle-test
            tests/sotn/overlay_pcm_lle_test.cpp)
        target_compile_definitions(overlay-pcm-lle-test PRIVATE
            SOTN_GAME_DIR="${SOTN_GAME_DIR}")
        target_link_libraries(overlay-pcm-lle-test PRIVATE
            SaturnSndlib::Sndlib
            SaturnPlayer::Sotn
        )
        add_test(NAME overlay-pcm-lle-oracle COMMAND overlay-pcm-lle-test)
        set_tests_properties(overlay-pcm-lle-oracle PROPERTIES TIMEOUT 3600)
    else ()
        message(STATUS
            "saturn-sound-player: SOTN corpus tests disabled; set "
            "SOTN_GAME_DIR to an extracted supported game directory")
    endif ()
endif ()

add_executable(saturn-sfx
    src/app/main.cpp
)
target_link_libraries(saturn-sfx PRIVATE
    SaturnPlayer::Core
    SaturnPlayer::Sotn
    SaturnPlayer::Scorcher
    SaturnSndlib::Sndlib
)

if (SDL3_FOUND)
    target_compile_definitions(saturn-sfx PRIVATE SATURN_SFX_HAVE_SDL=1)
endif ()
