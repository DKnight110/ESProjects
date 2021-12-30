cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(lightfantemp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_executable(lightfantemp)

pico_sdk_init()

pico_generate_pio_header(lightfantemp ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

#add_subdirectory()
target_include_directories(lightfantemp PRIVATE ../../pico-onewire/api)

target_sources(lightfantemp PRIVATE
	main.cpp
	serial_comms.c
	../../pico-onewire/source/one_wire.cpp
)

#target_compile_definitions(lightfantemp PRIVATE
#        ERR_LEVEL=3
#)

target_link_libraries(lightfantemp pico_stdlib hardware_pio hardware_uart)

pico_enable_stdio_usb(lightfantemp 1)
pico_enable_stdio_uart(lightfantemp 1)

pico_add_extra_outputs(lightfantemp)
