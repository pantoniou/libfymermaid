# vim: set ts=4:sw=4:expandtab:

# The API test carries its own assertions; it links the static archive so
# that it is testing exactly the objects this tree built.
add_executable(fymm-api-test fymm-api-test.c)
target_link_libraries(fymm-api-test PRIVATE libfymermaid::libfymermaid_static)
# the colour reduction is internal, so the test reaches into src/ for it
target_include_directories(fymm-api-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME api COMMAND fymm-api-test)

# The terminal background query needs a terminal to answer it, so this test
# makes one with a pty. Only where the platform has openpty().
include(CheckSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES util)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(openpty "pty.h" FYMM_HAVE_OPENPTY)
unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(FYMM_HAVE_OPENPTY)
    add_executable(fymm-pty-test fymm-pty-test.c)
    target_link_libraries(fymm-pty-test
        PRIVATE libfymermaid::libfymermaid_static util)
    add_test(NAME pty COMMAND fymm-pty-test)
endif()

# Golden files. Every test/<type>/<name>.mmd is rendered and compared against
# the golden files beside it: <name>.model, <name>.txt, <name>.ascii and, for a
# source that must fail, <name>.diag. A missing golden removes that case, so a
# source that only needs its diagnostics pinned carries only a .diag.
add_executable(fymm-golden-test fymm-golden-test.c)
target_link_libraries(fymm-golden-test PRIVATE libfymermaid::libfymermaid_static)

set(_golden_modes model unicode ascii diag)
set(_golden_ext_model ".model")
set(_golden_ext_unicode ".txt")
set(_golden_ext_ascii ".ascii")
set(_golden_ext_diag ".diag")

file(GLOB _golden_dirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _golden_dirs)
foreach(_type ${_golden_dirs})
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_type}")
    if(NOT IS_DIRECTORY "${_dir}" OR _type STREQUAL "mermaid-suite")
        continue()
    endif()

    file(GLOB _inputs CONFIGURE_DEPENDS "${_dir}/*.mmd")
    list(SORT _inputs)
    foreach(_in ${_inputs})
        get_filename_component(_name "${_in}" NAME_WE)
        foreach(_mode ${_golden_modes})
            set(_expected "${_dir}/${_name}${_golden_ext_${_mode}}")
            if(EXISTS "${_expected}")
                add_test(NAME "${_type}/${_name}/${_mode}"
                    COMMAND fymm-golden-test ${_mode} "${_in}" "${_expected}")
                set_tests_properties("${_type}/${_name}/${_mode}"
                    PROPERTIES LABELS golden)
            endif()
        endforeach()
    endforeach()
endforeach()

# The mermaid conformance corpus. Each case is one `it()` imported from an
# upstream parser spec by scripts/import-mermaid-suite.py: a diagram source,
# and whether upstream expects it to parse or to fail. See
# test/mermaid-suite/PROVENANCE.
#
# Every suite is registered, including the diagram types this library does not
# implement yet. An unimplemented suite is registered DISABLED and labelled
# `unimplemented`, so the corpus is present and countable, `ctest` stays green,
# and implementing a type turns its whole suite on at once.
#
# Each case is an independent process that reads only its own input, so the
# corpus runs under `ctest -j`.
set(FYMM_IMPLEMENTED_SUITES "gitgraph;pie;timeline;journey;mindmap;sequence;flowchart;class;quadrant;xychart;radar;gantt;state;er;kanban;usecase;c4;block;packet;architecture;info;ishikawa;treeView;eventmodeling;wardley;railroad" CACHE STRING
    "mermaid corpus suites expected to pass; set to a wider list to measure the gap")

set(_ms_dir "${CMAKE_CURRENT_SOURCE_DIR}/mermaid-suite")
file(GLOB _ms_manifests CONFIGURE_DEPENDS "${_ms_dir}/*/manifest.list")
list(SORT _ms_manifests)

set(_ms_total 0)
set(_ms_active 0)
foreach(_manifest ${_ms_manifests})
    get_filename_component(_suite_dir "${_manifest}" DIRECTORY)
    get_filename_component(_suite "${_suite_dir}" NAME)

    if(_suite IN_LIST FYMM_IMPLEMENTED_SUITES)
        set(_disabled FALSE)
        set(_label mermaid)
    else()
        set(_disabled TRUE)
        set(_label unimplemented)
    endif()

    # re-run configure when a manifest changes; CONFIGURE_DEPENDS on the
    # glob catches a new suite, not an edit to an existing one
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")

    file(STRINGS "${_manifest}" _cases)
    foreach(_case ${_cases})
        # each line is `<outcome>:<file>`
        string(FIND "${_case}" ":" _colon)
        string(SUBSTRING "${_case}" 0 ${_colon} _outcome)
        math(EXPR _rest "${_colon} + 1")
        string(SUBSTRING "${_case}" ${_rest} -1 _file)
        get_filename_component(_name "${_file}" NAME_WE)

        add_test(NAME "mermaid/${_suite}/${_name}"
            COMMAND fymm-golden-test ${_outcome} "${_suite_dir}/${_file}")
        set_tests_properties("mermaid/${_suite}/${_name}" PROPERTIES
            LABELS "${_label}" DISABLED ${_disabled})
        math(EXPR _ms_total "${_ms_total} + 1")
        if(NOT _disabled)
            math(EXPR _ms_active "${_ms_active} + 1")
        endif()
    endforeach()
endforeach()

message(STATUS
    "mermaid corpus: ${_ms_active} of ${_ms_total} cases active "
    "(implemented: ${FYMM_IMPLEMENTED_SUITES})")
