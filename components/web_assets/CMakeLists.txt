# ──────────────────────────────────────────────────────────────
# The built frontend, embedded in the application binary.
#
# Chain:  frontend/src/**  ->  (pnpm build)  ->  www/
#         www/             ->  (pack_web_assets.py)  ->  web_assets.bin
#         web_assets.bin   ->  (EMBED_FILES)  ->  rodata in the app image
#
# The UI therefore ships *with* the firmware: one artifact, one version, one
# update. There is no www partition to mount, to flash, or to get out of step
# with the application that serves it.
#
# It is one blob rather than one EMBED_FILES entry per asset because vite
# content-hashes its filenames, and EMBED_FILES needs its list when CMake
# configures — long before vite has run. A single fixed name sidesteps that
# entirely, and the blob's directory is the manifest a fetch protocol needs.
#
# Same format (KCWA v1) as the gateway's bundle, deliberately: whoever learns to
# read one reads the other.
# ──────────────────────────────────────────────────────────────

idf_build_get_property(python PYTHON)

set(FRONTEND_DIR    "${CMAKE_CURRENT_LIST_DIR}/../../frontend")
set(WWW_DIR         "${CMAKE_CURRENT_LIST_DIR}/../../www")
set(PACK_SCRIPT     "${CMAKE_CURRENT_LIST_DIR}/pack_web_assets.py")
set(WEB_ASSETS_BIN  "${CMAKE_CURRENT_BINARY_DIR}/web_assets.bin")
set(FRONTEND_STAMP  "${CMAKE_CURRENT_BINARY_DIR}/frontend.stamp")

idf_component_register(
    SRCS "WebAssets.cpp"
    INCLUDE_DIRS "."
    EMBED_FILES "${WEB_ASSETS_BIN}")

# Everything below runs only in the second pass — idf_component_register returns
# early while ESP-IDF is still collecting requirements, and registering these
# rules twice would be an error.

# What a frontend rebuild depends on. CONFIGURE_DEPENDS re-runs the glob when
# ninja checks the build, so a newly added .tsx is picked up without a manual
# reconfigure. node_modules is not globbed: pnpm owns it, and walking it would
# cost more than the whole build.
file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS "${FRONTEND_DIR}/src/*")
list(APPEND FRONTEND_SOURCES
    "${FRONTEND_DIR}/index.html"
    "${FRONTEND_DIR}/package.json"
    "${FRONTEND_DIR}/vite.config.ts")

# No pnpm on the PATH is common on Windows, where Node ships corepack but
# leaves its pnpm shim switched off. Corepack runs pnpm all the same; its
# download prompt is switched off for the same reason as pnpm's purge prompt.
# The Windows forms are named first: Node's folder also holds extensionless
# shell scripts for Git Bash under the same names, which Windows cannot run.
find_program(PNPM NAMES pnpm.exe pnpm.cmd pnpm)
find_program(COREPACK NAMES corepack.exe corepack.cmd corepack)
if(PNPM)
    set(PNPM_CMD "${PNPM}")
elseif(COREPACK)
    set(PNPM_CMD "${CMAKE_COMMAND}" -E env COREPACK_ENABLE_DOWNLOAD_PROMPT=0 "${COREPACK}" pnpm)
endif()

if(PNPM_CMD)
    # --no-frozen-lockfile lets a package.json edit update the lockfile rather
    # than failing a firmware build; --config.confirm-modules-purge=false skips
    # the prompt pnpm shows when node_modules is out of sync, which would
    # otherwise hang a build with nobody watching.
    add_custom_command(OUTPUT "${FRONTEND_STAMP}"
        COMMAND ${PNPM_CMD} install --no-frozen-lockfile --config.confirm-modules-purge=false
        COMMAND ${PNPM_CMD} build
        COMMAND "${CMAKE_COMMAND}" -E touch "${FRONTEND_STAMP}"
        WORKING_DIRECTORY "${FRONTEND_DIR}"
        DEPENDS ${FRONTEND_SOURCES}
        COMMENT "Building thermostat frontend (pnpm install && pnpm build)"
        VERBATIM USES_TERMINAL)
else()
    # The firmware must still build on a machine without node. The packer falls
    # back to a placeholder page, so the image always has a valid blob and no
    # code anywhere has to handle a missing one.
    message(STATUS "pnpm not found — the frontend will not be rebuilt")
    add_custom_command(OUTPUT "${FRONTEND_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${FRONTEND_STAMP}"
        VERBATIM)
endif()

# The packer's output is what EMBED_FILES names, so this rule is what teaches
# CMake how to produce it — the embed step depends on the file, this rule makes
# it, and the stamp in between forces the frontend to build first.
#
# It also depends on www/ itself. Without node the stamp never changes, so a
# build folder that first packed an empty www/ kept embedding the placeholder
# page after a `pnpm build` by hand had filled it, which is exactly the advice
# the placeholder gives.
file(GLOB_RECURSE WWW_FILES CONFIGURE_DEPENDS "${WWW_DIR}/*")
add_custom_command(OUTPUT "${WEB_ASSETS_BIN}"
    COMMAND "${python}" "${PACK_SCRIPT}" --src "${WWW_DIR}" --out "${WEB_ASSETS_BIN}"
    DEPENDS "${FRONTEND_STAMP}" "${PACK_SCRIPT}" ${WWW_FILES}
    COMMENT "Packing web assets into web_assets.bin"
    VERBATIM)
