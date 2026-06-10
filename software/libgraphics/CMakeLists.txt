add_library(libgraphics INTERFACE)

target_sources(libgraphics INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/graphics.c
)

target_include_directories(libgraphics INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(libgraphics INTERFACE
    pico_base_headers
    libcolor
)
