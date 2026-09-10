cmake_minimum_required(VERSION 3.20)
project(ZeroRacersVirtualBoyRecomp C CXX)
include(CTest)
set(VBRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vbrecomp" CACHE PATH "vbrecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
option(ZERO_RACERS_UI "Build the shared launcher and settings" ON)
set(VBRECOMP_GAME zero_racers CACHE STRING "Generated cartridge module" FORCE)
if(ZERO_RACERS_UI)
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
