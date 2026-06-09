# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(mod_player
	main.c
	micromod.c
)

target_compile_options(mod_player PRIVATE -Wall)

target_include_directories(mod_player PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_definitions(mod_player PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	)

if(DVI_SERIAL_USES_LARGE_GPIO STREQUAL 1)
	target_compile_definitions(mod_player PRIVATE
		PICO_PIO_USE_GPIO_BASE=1
	)
endif()

target_link_libraries(mod_player
	pico_stdlib
	pico_multicore
	pico_util
	hardware_timer
	libdvi
	libsprite
)

# create map/bin/hex file etc.
pico_add_extra_outputs(mod_player)
