add_library(libcolor INTERFACE)

target_sources(libcolor INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/color.c
    ${CMAKE_CURRENT_LIST_DIR}/color.h
)

target_include_directories(libcolor INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(libcolor INTERFACE
    pico_base_headers
)
