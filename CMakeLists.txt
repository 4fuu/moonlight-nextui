cmake_minimum_required(VERSION 3.16)

project(moonlight-nextui LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(NEXTUI_FULL_OVERLAY
       "Build the vendored NextUI full-fidelity overlay renderer" ON)
option(NEXTUI_SDK
       "Compile the nextui-app-sdk binding (requires a populated third_party/nextui-app-sdk)" OFF)
option(NEXTUI_BUILD_TESTS "Build host tests and the overlay render smoke test" OFF)

if(NOT DEFINED MOONLIGHT_PLATFORM)
    if(CMAKE_CROSSCOMPILING)
        set(MOONLIGHT_PLATFORM "tg5040")
    else()
        set(MOONLIGHT_PLATFORM "host")
    endif()
endif()

if(MOONLIGHT_PLATFORM STREQUAL "tg5040")
    set(MOONLIGHT_PLATFORM_DEFINE PLATFORM_TG5040)
    set(MOONLIGHT_PLATFORM_STRING "tg5040")
else()
    set(MOONLIGHT_PLATFORM_DEFINE PLATFORM_NEXTUI)
    set(MOONLIGHT_PLATFORM_STRING "nextui")
endif()

# ── SDL2 ────────────────────────────────────────────────────────────────────
# The tg5040 sysroot ships sdl2.pc but not SDL2_ttf.pc/SDL2_image.pc; their
# .so stubs are present. Fall back to find_library, as the upstream port
# Makefile did before this repo moved to CMake.

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

set(MOONLIGHT_SDL_LIBS PkgConfig::SDL2)
foreach(component ttf image)
    string(TOUPPER "${component}" COMPONENT_UPPER)
    pkg_check_modules(SDL2_${COMPONENT_UPPER} QUIET IMPORTED_TARGET SDL2_${component})
    if(SDL2_${COMPONENT_UPPER}_FOUND)
        list(APPEND MOONLIGHT_SDL_LIBS PkgConfig::SDL2_${COMPONENT_UPPER})
    else()
        find_library(SDL2_${COMPONENT_UPPER}_LIBRARY NAMES SDL2_${component} REQUIRED)
        list(APPEND MOONLIGHT_SDL_LIBS ${SDL2_${COMPONENT_UPPER}_LIBRARY})
        if(DEFINED SDL2_INCLUDE_DIRS)
            include_directories(${SDL2_INCLUDE_DIRS})
        endif()
    endif()
endforeach()

# ── Wrapper ─────────────────────────────────────────────────────────────────

set(MOONLIGHT_SOURCES
    src/main.c
    src/ui.c
    src/config.c
    src/moonlight_cli.c
    src/nextui_coop.c
)

add_executable(moonlight-pak ${MOONLIGHT_SOURCES})
target_include_directories(moonlight-pak PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/apostrophe/include
)
target_compile_definitions(moonlight-pak PRIVATE ${MOONLIGHT_PLATFORM_DEFINE})
target_link_libraries(moonlight-pak PRIVATE ${MOONLIGHT_SDL_LIBS} m pthread)

if(MOONLIGHT_PLATFORM STREQUAL "tg5040")
    target_compile_options(moonlight-pak PRIVATE
        -mcpu=cortex-a53 -mtune=cortex-a53
        -ffunction-sections -fdata-sections -fstack-protector-strong
    )
    target_link_options(moonlight-pak PRIVATE -Wl,--gc-sections)
endif()

# ── NextUI cooperation seam (M1-M6) ─────────────────────────────────────────
# src/nextui_coop.c is a no-op until the SDK C client exists. The option is
# kept explicit so a checkout with the placeholder submodule still builds.

if(NEXTUI_SDK)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nextui-app-sdk/include"
       AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nextui-app-sdk/src")
        message(FATAL_ERROR
            "NEXTUI_SDK=ON but third_party/nextui-app-sdk has no include/ or src/; "
            "the SDK C client has not landed yet (phase A).")
    endif()
    target_include_directories(moonlight-pak PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nextui-app-sdk/include)
    target_compile_definitions(moonlight-pak PRIVATE NEXTUI_SDK=1)
endif()

# ── Full-fidelity overlay renderer (vendored NextUI) ────────────────────────
# Build-only in S1-S5: M2 attaches it to the wrapper's main loop through the
# Apostrophe host seam. Building the archive every CI run proves the vendored
# closure cross-compiles for tg5040.

if(NEXTUI_FULL_OVERLAY)
    set(NEXTUI_OVERLAY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nextui-overlay)
    add_library(nextui-overlay STATIC
        ${NEXTUI_OVERLAY_DIR}/vendor/api.c
        ${NEXTUI_OVERLAY_DIR}/vendor/config.c
        ${NEXTUI_OVERLAY_DIR}/vendor/i18n.c
        ${NEXTUI_OVERLAY_DIR}/vendor/palette.c
        ${NEXTUI_OVERLAY_DIR}/vendor/utils.c
        ${NEXTUI_OVERLAY_DIR}/vendor/scaler.c
        ${NEXTUI_OVERLAY_DIR}/vendor/ui_overlay.c
        ${NEXTUI_OVERLAY_DIR}/vendor/apostrophe_overlay_host.c
        ${NEXTUI_OVERLAY_DIR}/src/platform_stubs.c
    )
    target_include_directories(nextui-overlay PUBLIC
        ${NEXTUI_OVERLAY_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/apostrophe/include
    )
    target_compile_definitions(nextui-overlay PUBLIC
        USE_SDL2
        PLATFORM="${MOONLIGHT_PLATFORM_STRING}"
    )
    target_compile_definitions(nextui-overlay PRIVATE ${MOONLIGHT_PLATFORM_DEFINE})
    target_compile_options(nextui-overlay PRIVATE
        -ffunction-sections -fdata-sections -Wno-unused-parameter)
    # api.c references libsamplerate from its audio path. NextUI stages it in
    # the sysroot; prefer pkg-config, then find_library, then the bare name.
    pkg_check_modules(SAMPLERATE QUIET IMPORTED_TARGET samplerate)
    if(SAMPLERATE_FOUND)
        set(MOONLIGHT_SAMPLERATE PkgConfig::SAMPLERATE)
    else()
        find_library(SAMPLERATE_LIBRARY NAMES samplerate)
        if(SAMPLERATE_LIBRARY)
            set(MOONLIGHT_SAMPLERATE ${SAMPLERATE_LIBRARY})
        else()
            set(MOONLIGHT_SAMPLERATE samplerate)
        endif()
    endif()

    target_link_libraries(nextui-overlay PUBLIC
        ${MOONLIGHT_SDL_LIBS} ${MOONLIGHT_SAMPLERATE} m pthread dl)
    # api.c is vendored whole; gc-sections keeps only the overlay path and
    # avoids pulling NextUI's platform-only functions (power, video, LEDs).
    target_link_options(nextui-overlay INTERFACE -Wl,--gc-sections)
endif()

# ── Tests ───────────────────────────────────────────────────────────────────

if(NEXTUI_BUILD_TESTS)
    enable_testing()

    add_executable(test_parsers tests/test_parsers.c)
    target_link_libraries(test_parsers PRIVATE m)
    add_test(NAME parsers COMMAND test_parsers)

    # Runs the real ml_stream() against a fake engine on PATH: argv
    # transparency, exit status, process group, and the cooperation state the
    # SDK pump relies on.
    add_executable(test_stream_spawn
        tests/test_stream_spawn.c
        tests/apostrophe_impl.c
        src/moonlight_cli.c
        src/nextui_coop.c
    )
    target_include_directories(test_stream_spawn PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/apostrophe/include)
    target_compile_definitions(test_stream_spawn PRIVATE ${MOONLIGHT_PLATFORM_DEFINE})
    target_link_libraries(test_stream_spawn PRIVATE ${MOONLIGHT_SDL_LIBS} m pthread)
    add_test(NAME stream-spawn COMMAND test_stream_spawn)

    if(NEXTUI_FULL_OVERLAY)
        # The host render test is hermetic: the library is compiled against a
        # scratch SD root, then scripts/nextui/test-overlay.sh populates it with
        # the vendored atlas and font. This only happens in a test build, so a
        # device build always keeps NextUI's /mnt/SDCARD default.
        set(NEXTUI_TEST_SD_ROOT "${CMAKE_CURRENT_BINARY_DIR}/nextui-test-sd")
        target_compile_definitions(nextui-overlay PUBLIC
            SDCARD_PATH="${NEXTUI_TEST_SD_ROOT}")

        add_executable(overlay-smoke
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nextui-overlay/tests/overlay_smoke.c)
        target_link_libraries(overlay-smoke PRIVATE nextui-overlay)
        add_test(NAME overlay-smoke
                 COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/nextui/test-overlay.sh
                         $<TARGET_FILE:overlay-smoke>
                         ${NEXTUI_TEST_SD_ROOT}
                         ${CMAKE_CURRENT_BINARY_DIR}/overlay-smoke.png)
    endif()
endif()

install(TARGETS moonlight-pak RUNTIME DESTINATION .)
