# vim: set ts=4:sw=4:expandtab:
add_executable(fy-mermaid-cli fy-mermaid.c)
set_target_properties(fy-mermaid-cli PROPERTIES OUTPUT_NAME fy-mermaid)
target_link_libraries(fy-mermaid-cli PRIVATE libfymermaid::libfymermaid)

install(TARGETS fy-mermaid-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES fy-mermaid.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
