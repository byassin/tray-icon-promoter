cmake_minimum_required(VERSION 3.20)

project(TrayIconPromoter VERSION 1.1.1 LANGUAGES C RC)

add_executable(TrayIconPromoter WIN32
    src/tray_icon_promoter.c
    resources/version.rc
)

target_compile_features(TrayIconPromoter PRIVATE c_std_11)
target_include_directories(TrayIconPromoter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
target_compile_definitions(TrayIconPromoter PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_link_libraries(TrayIconPromoter PRIVATE advapi32)

if(MSVC)
    target_compile_options(TrayIconPromoter PRIVATE
        /O1
        /GL
        /GS
        /guard:cf
        /sdl
        /W4
        /WX
        /permissive-
    )
    target_link_options(TrayIconPromoter PRIVATE
        /LTCG
        /OPT:REF
        /OPT:ICF
        /DYNAMICBASE
        /NXCOMPAT
        /HIGHENTROPYVA
        /guard:cf
        /MANIFEST:NO
    )
else()
    target_compile_options(TrayIconPromoter PRIVATE
        -Os
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(TrayIconPromoter PRIVATE
        -municode
        -mwindows
        -static-libgcc
        -Wl,--gc-sections
        -Wl,--dynamicbase
        -Wl,--nxcompat
        -Wl,--high-entropy-va
    )
endif()

set_target_properties(TrayIconPromoter PROPERTIES
    OUTPUT_NAME "TrayIconPromoter"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(TrayIconPromoterTests tests/native_tests.c)
    target_compile_features(TrayIconPromoterTests PRIVATE c_std_11)
    target_compile_definitions(TrayIconPromoterTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(TrayIconPromoterTests PRIVATE advapi32)
    if(MSVC)
        target_compile_options(TrayIconPromoterTests PRIVATE /W4 /WX /sdl)
    else()
        target_compile_options(TrayIconPromoterTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME NativeArgumentsAndReadiness COMMAND TrayIconPromoterTests)
    set_tests_properties(NativeArgumentsAndReadiness PROPERTIES TIMEOUT 30)
endif()
