# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Version resolution:
#   CI passes -DSOFTWARE_VERSION_MAJOR/MINOR/PATCH parsed from the VX.Y.Z tag.
#   Local builds derive it from the nearest uppercase-V tag (git describe);
#   fall back to 0.0.0-dev when git or a matching tag is unavailable.
if(DEFINED SOFTWARE_VERSION_MAJOR)
    set(PROJECT_VER "${SOFTWARE_VERSION_MAJOR}.${SOFTWARE_VERSION_MINOR}.${SOFTWARE_VERSION_PATCH}")
else()
    execute_process(
        COMMAND git describe --tags --abbrev=0 --match "V*"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^V0*([0-9]+)\\.0*([0-9]+)\\.0*([0-9]+)$")
        set(PROJECT_VER "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    else()
        set(PROJECT_VER "0.0.0-dev")
    endif()
endif()

# ── Per-board sdkconfig ───────────────────────────────────────
# Common defaults plus an optional per-board overlay living in the board
# folder (main/hardware/boards/<board>/sdkconfig.defaults), letting a board
# change flash size / PSRAM / partitions without touching the shared
# defaults. sdkconfig fragments cannot include one another, so this list is
# the composition point: the board file overlays the common one and wins on
# conflicts. BOARD is the same cache var consumed by main/CMakeLists.txt.
if(NOT DEFINED BOARD)
    set(BOARD "diyless_thermostat_3")
endif()
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_SOURCE_DIR}/main/hardware/boards/${BOARD}/sdkconfig.defaults")
    list(APPEND SDKCONFIG_DEFAULTS "main/hardware/boards/${BOARD}/sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(Thermostat)

# ── Frontend ──────────────────────────────────────────────────
# Nothing here: the built frontend is embedded in the application image by
# components/web_assets, which owns the pnpm build, the packing and the embed.
# The UI ships with the firmware — one artifact, one version, one update — so
# there is no www partition and no FAT image to generate.
