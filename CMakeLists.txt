cmake_minimum_required(VERSION 3.20)
project(ZeroRacersVirtualBoyRecomp VERSION 0.0.2 LANGUAGES C CXX)
include(CTest)
set(VBRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vbrecomp" CACHE PATH "vbrecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
option(ZERO_RACERS_UI "Build the shared launcher and settings" ON)
if(NOT EXISTS "${VBRECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "Initialize dependencies with git submodule update --init --recursive, or set VBRECOMP_ROOT.")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(_color_package "${CMAKE_CURRENT_BINARY_DIR}/mod-packages/zero-racers-full-color-0.2.1.vbmod")
    add_custom_command(OUTPUT "${_color_package}"
        COMMAND "${Python3_EXECUTABLE}" "${VBRECOMP_ROOT}/tools/pack_mod.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/mods/full-color" "${_color_package}"
        DEPENDS mods/full-color/manifest.toml mods/full-color/palette.txt mods/full-color/materials.txt mods/full-color/models.txt
            "${VBRECOMP_ROOT}/tools/pack_mod.py"
        VERBATIM)
    add_custom_target(zero-racers-mods ALL DEPENDS "${_color_package}")
endif()
set(VBRECOMP_GAME zero_racers CACHE STRING "Generated cartridge module" FORCE)
if(ZERO_RACERS_UI)
    if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
        message(FATAL_ERROR "Initialize recomp-ui with git submodule update --init --recursive, or set RECOMP_UI_ROOT.")
    endif()
    set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL "Mod catalog" FORCE)
    include("${RECOMP_UI_ROOT}/recomp_ui.cmake")
endif()
add_subdirectory("${VBRECOMP_ROOT}" vbrecomp)
target_compile_definitions(vb-runtime PRIVATE
    VB_GAME_ID="zero-racers"
    VB_GAME_TITLE="Zero Racers Recompiled"
    VB_GAME_SHA256="47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7"
    VB_GAME_CRC32=0x71553796u)
set_target_properties(vb-runtime PROPERTIES OUTPUT_NAME ZeroRacersVirtualBoyRecomp)
target_sources(vb-runtime PRIVATE src/zero_racers_color.cpp)
if(ZERO_RACERS_UI)
    add_custom_target(zero-racers-assets
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vb-runtime>/assets/img"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/boxart.tga"
            "$<TARGET_FILE_DIR:vb-runtime>/assets/img/boxart.tga"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.md"
            "$<TARGET_FILE_DIR:vb-runtime>/assets/GAME-ART-NOTICE.md"
        VERBATIM)
    add_dependencies(vb-runtime zero-racers-assets)
endif()
