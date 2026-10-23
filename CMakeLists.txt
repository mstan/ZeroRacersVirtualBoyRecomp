cmake_minimum_required(VERSION 3.20)
project(ZeroRacersVirtualBoyRecomp VERSION 0.0.5 LANGUAGES C CXX)
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

add_library(zero-racers-routine STATIC src/zero_racers_routine.cpp src/zero_racers_actor.cpp
    src/zero_racers_bus.cpp src/zero_racers_world.cpp src/zero_racers_camera.cpp src/zero_racers_session.cpp src/zero_racers_font.cpp src/zero_racers_menu.cpp)
target_include_directories(zero-racers-routine PUBLIC src "${VBRECOMP_ROOT}/runtime/include")
target_compile_features(zero-racers-routine PUBLIC cxx_std_17)
if(BUILD_TESTING)
    set(_zr_test_core
        "${VBRECOMP_ROOT}/runtime/src/v810_interpreter.c"
        "${VBRECOMP_ROOT}/runtime/src/v810_extended.c"
        "${VBRECOMP_ROOT}/runtime/src/interrupts.c"
        "${VBRECOMP_ROOT}/runtime/src/vb_crc32.c")
    add_executable(zero-racers-routine-test tests/test_native_routine.cpp
        ${_zr_test_core})
    add_executable(zero-racers-session-test tests/test_versus_session.cpp ${_zr_test_core}
        "${VBRECOMP_ROOT}/runtime/src/png_write.c")
    target_link_libraries(zero-racers-session-test PRIVATE zero-racers-routine)
    target_link_libraries(zero-racers-routine-test PRIVATE zero-racers-routine)
    if(NOT MSVC)
        target_link_libraries(zero-racers-routine-test PRIVATE m)
        target_link_libraries(zero-racers-session-test PRIVATE m)
    endif()
    option(ZERO_RACERS_NATIVE_TESTS "Build the generated-native versus qualification runner" OFF)
    if(ZERO_RACERS_NATIVE_TESTS)
        include("${CMAKE_CURRENT_SOURCE_DIR}/generated/zero_racers_sources.cmake")
        set(_zr_native_sources "${CMAKE_CURRENT_SOURCE_DIR}/generated/zero_racers_dispatch.c")
        foreach(_source IN LISTS VB_GENERATED_BODY_SOURCES)
            list(APPEND _zr_native_sources "${CMAKE_CURRENT_SOURCE_DIR}/generated/${_source}")
        endforeach()
        add_executable(zero-racers-session-native-test tests/test_versus_session.cpp ${_zr_test_core}
            "${VBRECOMP_ROOT}/runtime/src/png_write.c" ${_zr_native_sources})
        target_compile_definitions(zero-racers-session-native-test PRIVATE ZR_TEST_NATIVE=1)
        target_link_libraries(zero-racers-session-native-test PRIVATE zero-racers-routine)
        if(NOT MSVC)
            target_link_libraries(zero-racers-session-native-test PRIVATE m)
        endif()
    endif()
endif()
target_compile_definitions(vb-runtime PRIVATE
    VB_GAME_LOCAL_PLAYERS=4
    VB_GAME_ID="zero-racers"
    VB_GAME_TITLE="Zero Racers Recompiled"
    VB_GAME_SHA256="47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7"
    VB_GAME_CRC32=0x71553796u)
set_target_properties(vb-runtime PROPERTIES OUTPUT_NAME ZeroRacersVirtualBoyRecomp)
target_sources(vb-runtime PRIVATE src/zero_racers_color.cpp src/zero_racers_host.cpp src/zero_racers_video.cpp)
target_compile_definitions(vb-runtime PRIVATE VB_SIMULATION_POINTS=1)
target_link_libraries(vb-runtime PRIVATE zero-racers-routine)
vb_state_add_identity(vb-runtime src/zero_racers_color.cpp src/zero_racers_host.cpp
    src/zero_racers_routine.h src/zero_racers_routine.cpp src/zero_racers_actor.h src/zero_racers_actor.cpp
    src/zero_racers_bus.h src/zero_racers_bus.cpp src/zero_racers_world.h src/zero_racers_world.cpp
    src/zero_racers_camera.h src/zero_racers_camera.cpp src/zero_racers_session.h src/zero_racers_session.cpp
    src/zero_racers_font.h src/zero_racers_font.cpp src/zero_racers_video.h src/zero_racers_video.cpp
    src/zero_racers_menu.h src/zero_racers_menu.cpp CMakeLists.txt)
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
