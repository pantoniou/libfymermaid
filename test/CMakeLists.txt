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
