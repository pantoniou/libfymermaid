# vim: set ts=4:sw=4:expandtab:

# The API test carries its own assertions; it links the static archive so
# that it is testing exactly the objects this tree built.
add_executable(fymm-api-test fymm-api-test.c)
target_link_libraries(fymm-api-test PRIVATE libfymermaid::libfymermaid_static)
add_test(NAME api COMMAND fymm-api-test)

# Golden files.  Every test/gitgraph/<name>.mmd is rendered three ways and
# compared against <name>.model, <name>.txt and <name>.ascii; a <name>.diag
# file, where one exists, pins the diagnostics instead.  Each pairing is its
# own ctest so a failure names the mode that broke.
add_executable(fymm-golden-test fymm-golden-test.c)
target_link_libraries(fymm-golden-test PRIVATE libfymermaid::libfymermaid_static)

set(_gg_dir "${CMAKE_CURRENT_SOURCE_DIR}/gitgraph")
file(GLOB _gg_inputs CONFIGURE_DEPENDS "${_gg_dir}/*.mmd")
list(SORT _gg_inputs)

foreach(_in ${_gg_inputs})
    get_filename_component(_name "${_in}" NAME_WE)
    foreach(_mode model unicode ascii diag)
        if(_mode STREQUAL "model")
            set(_ext ".model")
        elseif(_mode STREQUAL "unicode")
            set(_ext ".txt")
        elseif(_mode STREQUAL "ascii")
            set(_ext ".ascii")
        else()
            set(_ext ".diag")
        endif()
        set(_expected "${_gg_dir}/${_name}${_ext}")
        if(EXISTS "${_expected}")
            add_test(NAME "gitgraph/${_name}/${_mode}"
                COMMAND fymm-golden-test ${_mode} "${_in}" "${_expected}")
            set_tests_properties("gitgraph/${_name}/${_mode}"
                PROPERTIES LABELS gitgraph)
        endif()
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
set(FYMM_IMPLEMENTED_SUITES gitgraph CACHE STRING
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
