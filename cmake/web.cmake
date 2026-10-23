# Standalone ST0 browser build. Native builds never include this file.
psyz_web_configure_target(sotn OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/site")
get_target_property(web_link_options sotn LINK_OPTIONS)
list(FILTER web_link_options EXCLUDE REGEX "^--shell-file=")
set_property(TARGET sotn PROPERTY LINK_OPTIONS "${web_link_options}")
target_link_options(sotn PRIVATE
    --emit-symbol-map
    -sMAIN_MODULE=2
    -sEXIT_RUNTIME=1
    "$<TARGET_FILE:st0>"
    -sFORCE_FILESYSTEM=1
    "-sEXPORTED_RUNTIME_METHODS=['callMain','FS']"
    "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
    "--preload-file=${CMAKE_BINARY_DIR}/web-data@/"
    "--preload-file=$<TARGET_FILE:st0>@/st0.wasm")
target_compile_options(sotn PRIVATE -sASYNCIFY)

set(WEB_DATA_STAMP "${CMAKE_BINARY_DIR}/web-data.stamp")
file(GLOB WEB_STAGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/st/st0/*.c")
add_custom_command(OUTPUT "${WEB_DATA_STAMP}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/pack_web.py"
        --output "${CMAKE_BINARY_DIR}/web-data"
    COMMAND "${CMAKE_COMMAND}" -E touch "${WEB_DATA_STAMP}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/pack_web.py"
        "${CMAKE_SOURCE_DIR}/assets/st/st0/cutscene_script_psx.yaml"
        "${CMAKE_SOURCE_DIR}/src/dra/gen/us/xa_music_config.h"
        "${CMAKE_SOURCE_DIR}/src/dra/game_handlers.c"
        "${CMAKE_SOURCE_DIR}/include/sfx.h"
        "${CMAKE_SOURCE_DIR}/include/lba_us.h"
        ${WEB_STAGE_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Packing prologue data and original CD audio for the browser")
add_custom_target(web_data DEPENDS "${WEB_DATA_STAMP}")
add_dependencies(sotn web_data st0)
set_property(TARGET sotn APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/web/shell.html" "${WEB_DATA_STAMP}")
