# vim: set ts=4:sw=4:expandtab:
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)

# Handle absolute include and lib dirs outside of CMAKE_INSTALL_PREFIX
include(JoinPaths.cmake)
join_paths(PKGCONFIG_INCLUDEDIR "\${prefix}" "${CMAKE_INSTALL_INCLUDEDIR}")
join_paths(PKGCONFIG_LIBDIR "\${prefix}" "${CMAKE_INSTALL_LIBDIR}")

# libfymd4c reads the inline markdown a label may carry: mermaid's markdown
# strings, `["`**bold**`"]`, format the text inside them. It exposes that as
# fymd_inline_*, with md4c absorbed and hidden behind it, so this library
# links libfymd4c and never md4c itself.
if(NOT TARGET libfymd4c::libfymd4c)
    find_package(libfymd4c CONFIG QUIET)
endif()
if(TARGET libfymd4c::libfymd4c)
    get_target_property(LIBFYMD4C_INCLUDE_DIR libfymd4c::libfymd4c
                        INTERFACE_INCLUDE_DIRECTORIES)
    set(_fymm_fymd4c_target libfymd4c::libfymd4c)
else()
    find_path(LIBFYMD4C_INCLUDE_DIR NAMES libfymd4c.h)
    find_library(LIBFYMD4C_LIBRARY NAMES fymd4c)
    if(NOT LIBFYMD4C_INCLUDE_DIR OR NOT LIBFYMD4C_LIBRARY)
        message(FATAL_ERROR "libfymd4c not found (required: labels may carry markdown)")
    endif()
    set(_fymm_fymd4c_target "${LIBFYMD4C_LIBRARY}")
endif()

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

# Embed themes/*.yaml as byte arrays, so a built library carries its whole
# catalogue and needs nothing installed beside it. The name and description
# are lifted here as well, for fymm_theme_iterate(). Editing a theme re-runs
# configure through CMAKE_CONFIGURE_DEPENDS.
set(_fymm_themes_dir "${CMAKE_CURRENT_SOURCE_DIR}/../themes")
file(GLOB _fymm_theme_files CONFIGURE_DEPENDS "${_fymm_themes_dir}/*.yaml")
list(SORT _fymm_theme_files)
set(_fymm_themes_inc "${CMAKE_CURRENT_BINARY_DIR}/generated/fymm_themes.inc")
file(WRITE "${_fymm_themes_inc}" "/* Generated from the themes directory. */\n")
set(_fymm_theme_table "")
set(_fymm_theme_count 0)
foreach(_theme_file IN LISTS _fymm_theme_files)
    get_filename_component(_theme "${_theme_file}" NAME_WE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_theme_file}")
    file(READ "${_theme_file}" _theme_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," _theme_bytes "${_theme_hex}")
    string(TOUPPER "${_theme}" _theme_upper)
    # the one-line summary shown by --list-themes
    file(STRINGS "${_theme_file}" _theme_descr REGEX "^description:")
    string(REGEX REPLACE "^description:[ \t]*" "" _theme_descr "${_theme_descr}")
    string(REPLACE "\"" "\\\"" _theme_descr "${_theme_descr}")
    # a `;` in the text arrives escaped, because CMake strings are lists
    string(REPLACE "\\;" ";" _theme_descr "${_theme_descr}")
    file(APPEND "${_fymm_themes_inc}"
        "static const unsigned char FYMM_THEME_${_theme_upper}[] = {${_theme_bytes}};\n")
    string(APPEND _fymm_theme_table
        "\t{ \"${_theme}\", \"${_theme_descr}\", FYMM_THEME_${_theme_upper}, sizeof(FYMM_THEME_${_theme_upper}) },\n")
    math(EXPR _fymm_theme_count "${_fymm_theme_count} + 1")
endforeach()
if(_fymm_theme_count EQUAL 0)
    message(FATAL_ERROR "no themes found in ${_fymm_themes_dir}")
endif()
file(APPEND "${_fymm_themes_inc}"
    "static const struct fymm_embedded_theme FYMM_THEMES[] = {\n${_fymm_theme_table}};\n"
    "#define FYMM_THEME_COUNT ${_fymm_theme_count}\n"
    "#define FYMM_THEME_COUNT_MAX ${_fymm_theme_count}\n")

set(_fymm_incdir "${CMAKE_CURRENT_SOURCE_DIR}/../include")

set(_fymm_sources
    libfymermaid.c
    fymm-lex.c
    fymm-gitgraph.c
    fymm-common.c
    fymm-canvas.c fymm-canvas.h
    fymm-layout.c fymm-layout.h
    fymm-metrics.c
    fymm-legend.c fymm-legend.h
    fymm-frame.c fymm-frame.h
    fymm-braille.c fymm-braille.h
    fymm-markdown.c fymm-markdown.h
    fymm-color.c fymm-color.h
    fymm-theme.c
    fymm-render.c fymm-term.c
    fymm-select.c
    fymm-pie.c fymm-render-pie.c
    fymm-timeline.c fymm-render-timeline.c
    fymm-journey.c fymm-render-journey.c
    fymm-mindmap.c fymm-render-mindmap.c
    fymm-sequence.c fymm-render-sequence.c
    fymm-flowchart.c fymm-render-flowchart.c
    fymm-class.c fymm-render-class.c
    fymm-quadrant.c fymm-render-quadrant.c
    fymm-xychart.c fymm-render-xychart.c
    fymm-radar.c fymm-render-radar.c
    fymm-gantt.c fymm-render-gantt.c
    fymm-state.c fymm-render-state.c
    fymm-er.c fymm-render-er.c
    fymm-kanban.c fymm-render-kanban.c
    fymm-usecase.c fymm-render-usecase.c
    fymm-c4.c fymm-render-c4.c
    fymm-block.c fymm-render-block.c
    fymm-packet.c fymm-render-packet.c
    fymm-architecture.c fymm-render-architecture.c
    fymm-eventmodeling.c fymm-render-eventmodeling.c
    fymm-wardley.c fymm-render-wardley.c
    fymm-railroad.c fymm-render-railroad.c
    fymm-info.c fymm-render-info.c
    fymm-ishikawa.c fymm-render-ishikawa.c
    fymm-treeview.c fymm-render-treeview.c
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
    target_include_directories(${_t} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_include_directories(${_t} PRIVATE ${LIBFYMD4C_INCLUDE_DIR})
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

# the xy chart scales its plot with the C maths library
foreach(_t fymermaid fymermaid_static)
    target_link_libraries(${_t} PRIVATE m ${_fymm_fymd4c_target})
endforeach()

# An installed Mach-O binary finds its library through the library's own
# install name. The default @rpath ID leaves a consumer with no LC_RPATH
# to resolve, so name the installed library by its absolute path, as an
# absolute soname does on ELF.
if(APPLE)
    set_target_properties(fymermaid PROPERTIES
        INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()

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
install(FILES ${_fymm_theme_files}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/libfymermaid/themes)
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
