# vim: set ts=4:sw=4:expandtab:
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)

# Handle absolute include and lib dirs outside of CMAKE_INSTALL_PREFIX
include(JoinPaths.cmake)
join_paths(PKGCONFIG_INCLUDEDIR "\${prefix}" "${CMAKE_INSTALL_INCLUDEDIR}")
join_paths(PKGCONFIG_LIBDIR "\${prefix}" "${CMAKE_INSTALL_LIBDIR}")

# libfyaml is not an implementation detail here: the diagram model handed out
# by fymm_diagram_model() is a fy_generic, so the public header includes
# libfyaml's.  Reuse a parent-provided imported target, then libfyaml's own
# CMake package, and only then fall back to a raw path lookup.
if(NOT TARGET libfyaml::libfyaml)
    find_package(libfyaml CONFIG QUIET)
endif()
if(TARGET libfyaml::libfyaml)
    get_target_property(LIBFYAML_INCLUDE_DIR libfyaml::libfyaml INTERFACE_INCLUDE_DIRECTORIES)
else()
    find_path(LIBFYAML_INCLUDE_DIR NAMES libfyaml.h)
    find_library(LIBFYAML_LIBRARY NAMES fyaml)
    if(NOT LIBFYAML_INCLUDE_DIR OR NOT LIBFYAML_LIBRARY)
        message(FATAL_ERROR "libfyaml not found (required: the diagram model is a fy_generic)")
    endif()
endif()

if(TARGET libfyaml::libfyaml)
    set(_fymm_fyaml_shared libfyaml::libfyaml)
else()
    set(_fymm_fyaml_shared "${LIBFYAML_LIBRARY}")
endif()
if(TARGET libfyaml::fyaml_static)
    set(_fymm_fyaml_static libfyaml::fyaml_static)
else()
    set(_fymm_fyaml_static "${_fymm_fyaml_shared}")
endif()

configure_file(libfymermaid.pc.in libfymermaid.pc @ONLY)

set(_fymm_incdir "${CMAKE_CURRENT_SOURCE_DIR}/../include")

set(_fymm_sources
    libfymermaid.c
    fymm-lex.c
    fymm-gitgraph.c
    fymm-canvas.c fymm-canvas.h
    fymm-render.c
    fymm-internal.h
)

if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--exclude-libs,ALL")
    check_c_source_compiles("int main(void){return 0;}" FYMM_HAVE_EXCLUDE_LIBS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
endif()

# Two flavours from one source list, mirroring libfyaml and libfymd4c:
#   fymermaid        - the primary library (shared when BUILD_SHARED_LIBS)
#   fymermaid_static - a plain static archive for the tests and for tools
add_library(fymermaid ${_fymm_sources})
add_library(fymermaid_static STATIC ${_fymm_sources})

# Keep the static archive named libfymermaid.a next to libfymermaid.so on ELF.
set(_fymm_static_output fymermaid_static)
if(BUILD_SHARED_LIBS AND NOT WIN32)
    set(_fymm_static_output fymermaid)
endif()

foreach(_t fymermaid fymermaid_static)
    target_include_directories(${_t} PUBLIC
        "$<BUILD_INTERFACE:${_fymm_incdir}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        "$<BUILD_INTERFACE:${LIBFYAML_INCLUDE_DIR}>"
    )
    target_compile_definitions(${_t} PRIVATE "FYMM_VERSION_STRING=\"${PROJECT_VERSION}\"")
    target_compile_definitions(${_t} PRIVATE "$<$<CONFIG:Debug>:DEBUG>")
    if(CMAKE_C_COMPILER_ID MATCHES GNU|Clang)
        # Only FYMM_EXPORT-tagged fymm_* symbols leave the shared object.
        target_compile_options(${_t} PRIVATE -fvisibility=hidden)
        if(FYMM_HAVE_EXCLUDE_LIBS)
            target_link_options(${_t} PRIVATE "-Wl,--exclude-libs,ALL")
        endif()
    endif()
    if(HAVE_ASAN)
        target_compile_options(${_t} PRIVATE ${ASAN_C_FLAGS})
        target_link_options(${_t} PRIVATE ${ASAN_C_FLAGS} INTERFACE ${ASAN_C_FLAGS})
    endif()
endforeach()

if(BUILD_SHARED_LIBS)
    target_link_libraries(fymermaid PUBLIC ${_fymm_fyaml_shared})
else()
    target_link_libraries(fymermaid PUBLIC ${_fymm_fyaml_static})
endif()
target_link_libraries(fymermaid_static PUBLIC ${_fymm_fyaml_static})

set_target_properties(fymermaid PROPERTIES
    OUTPUT_NAME fymermaid
    VERSION ${FYMM_VERSION_FULL}
    SOVERSION ${FYMM_SOVERSION}
    PUBLIC_HEADER "${_fymm_incdir}/libfymermaid.h"
    EXPORT_NAME fymermaid
)
set_target_properties(fymermaid_static PROPERTIES
    OUTPUT_NAME ${_fymm_static_output}
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME fymermaid_static
)

add_library(libfymermaid::libfymermaid        ALIAS fymermaid)
add_library(libfymermaid::libfymermaid_static ALIAS fymermaid_static)

install(
    TARGETS fymermaid fymermaid_static
    EXPORT libfymermaid-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES
    "${_fymm_incdir}/libfymermaid/libfymermaid-util.h"
    "${_fymm_incdir}/libfymermaid/libfymermaid-diagram.h"
    "${_fymm_incdir}/libfymermaid/libfymermaid-render.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libfymermaid)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libfymermaid.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

install(EXPORT libfymermaid-targets
    FILE libfymermaid-targets.cmake
    NAMESPACE libfymermaid::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfymermaid)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/libfymermaid-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libfymermaid-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfymermaid)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libfymermaid-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libfymermaid-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libfymermaid-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libfymermaid)
