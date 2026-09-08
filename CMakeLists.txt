# vim: set ts=4:sw=4:expandtab:
cmake_minimum_required(VERSION 3.14)

project(libfymermaid
    VERSION "0.0.2"
    HOMEPAGE_URL "https://github.com/pantoniou/libfymermaid"
    DESCRIPTION "Mermaid diagram renderer for the terminal"
    LANGUAGES C
)

option(BUILD_FYMERMAID_EXECUTABLE "Whether to compile the fy-mermaid executable" ON)
option(ENABLE_ASAN "Enable ASAN support" OFF)
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

include(CTest)

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE $ENV{CMAKE_BUILD_TYPE})
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
    add_compile_options(-Wall -Wextra -Wsign-compare -Wdeclaration-after-statement)

    # Build against the newest C standard the compiler supports, mirroring
    # libfyaml: gnu2x -> c2x -> gnu11.  The libfyaml generic API leans on
    # _Generic, __VA_OPT__ and statement expressions, so gnu2x is what this
    # tree is actually developed against.
    include(CheckCCompilerFlag)
    set(_fymm_std "")
    check_c_compiler_flag(-std=gnu2x FYMM_HAS_GNU2X)
    if(FYMM_HAS_GNU2X)
        set(_fymm_std -std=gnu2x)
    else()
        check_c_compiler_flag(-std=c2x FYMM_HAS_C2X)
        if(FYMM_HAS_C2X)
            set(_fymm_std -std=c2x)
        else()
            check_c_compiler_flag(-std=gnu11 FYMM_HAS_GNU11)
            if(FYMM_HAS_GNU11)
                set(_fymm_std -std=gnu11)
            endif()
        endif()
    endif()
    if(_fymm_std)
        add_compile_options(${_fymm_std})
    endif()

    add_compile_definitions(_GNU_SOURCE)
endif()

# ASAN support (address + UB + signed-overflow sanitizers), as in libfyaml.
set(HAVE_ASAN 0)
set(ASAN_C_FLAGS)
if(ENABLE_ASAN)
    set(ASAN_C_FLAGS "-fsanitize=address,signed-integer-overflow,undefined"
                     "-fno-omit-frame-pointer")
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "${ASAN_C_FLAGS}")
    string(REPLACE ";" " " CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    check_c_source_compiles("int main(void) { return 0; }" ASAN_WORKS)
    unset(CMAKE_REQUIRED_FLAGS)
    if(ASAN_WORKS)
        set(HAVE_ASAN 1)
    else()
        message(FATAL_ERROR "ENABLE_ASAN is on but the sanitizer flags do not work")
    endif()
endif()

include(GNUInstallDirs)

# Shared-library version (soname) from .libtool-version (current:revision:age),
# exactly as libfyaml does.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.libtool-version" _LIBTOOL_VERSION)
string(REPLACE ":" ";" _LT_LIST "${_LIBTOOL_VERSION}")
list(GET _LT_LIST 0 LIBTOOL_CURRENT)
list(GET _LT_LIST 1 LIBTOOL_REVISION)
list(GET _LT_LIST 2 LIBTOOL_AGE)
math(EXPR FYMM_SOVERSION "${LIBTOOL_CURRENT} - ${LIBTOOL_AGE}")
set(FYMM_VERSION_FULL "${FYMM_SOVERSION}.${LIBTOOL_AGE}.${LIBTOOL_REVISION}")

add_subdirectory(src)

if(BUILD_FYMERMAID_EXECUTABLE)
    add_subdirectory(fy-mermaid)
endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
