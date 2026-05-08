# Header-only event model — no compiled sources.
add_library(lob_events INTERFACE)
target_include_directories(lob_events INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lob_events INTERFACE lob_core)
