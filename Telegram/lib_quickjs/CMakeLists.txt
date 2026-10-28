# This is the source code of LuxuryGram for Desktop.
# See LEGAL for license and copyright information.

# QuickJS-ng for the plugin engine, built the way its upstream
# CMakeLists.txt (v0.17.0) builds the qjs library. A separate target keeps
# the client's /W4 /WX and -Werror options off the vendored C sources.

add_library(luxury_quickjs STATIC)
add_library(luxurygram::lib_quickjs ALIAS luxury_quickjs)

get_filename_component(quickjs_loc ../SourceFiles/luxury/libs/quickjs REALPATH)

target_sources(luxury_quickjs
PRIVATE
    ${quickjs_loc}/dtoa.c
    ${quickjs_loc}/libregexp.c
    ${quickjs_loc}/libunicode.c
    ${quickjs_loc}/quickjs.c
)

set_target_properties(luxury_quickjs PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden
    # CI configures with CMAKE_COMPILE_WARNING_AS_ERROR=ON; vendored code
    # keeps its warnings visible but never fails the build on them.
    COMPILE_WARNING_AS_ERROR OFF
)

target_compile_definitions(luxury_quickjs PRIVATE
    QUICKJS_NG_BUILD
    _GNU_SOURCE
)

if (WIN32)
    target_compile_definitions(luxury_quickjs PRIVATE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601
    )
endif()

if (MSVC)
    target_compile_options(luxury_quickjs PRIVATE
        /experimental:c11atomics
        /wd4018
        /wd4061
        /wd4100
        /wd4200
        /wd4242
        /wd4244
        /wd4245
        /wd4267
        /wd4388
        /wd4389
        /wd4456
        /wd4457
        /wd4710
        /wd4711
        /wd4820
        /wd4996
        /wd5045
    )
else()
    target_compile_options(luxury_quickjs PRIVATE
        -funsigned-char
        -Wno-implicit-fallthrough
        -Wno-sign-compare
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wno-unused-result
        -Wno-array-bounds
    )
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(luxury_quickjs PRIVATE -Wno-stringop-truncation)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(luxury_quickjs PUBLIC m ${CMAKE_DL_LIBS} Threads::Threads)
endif()
