cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

set(project_name "pico-synth")
project(${project_name})
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Add I2S Audio Libraries
add_subdirectory(lib/pico_audio_i2s_32b)
add_subdirectory(lib/pico_audio_i2s_32b/src/pico_audio_32b)

set(bin_name ${PROJECT_NAME})
add_executable(${bin_name}
    src/main.cpp
    # quadrature_encoder.c  # Add the encoder source file
)

# Enable USB and UART for debugging
pico_enable_stdio_usb(${bin_name} 1)
pico_enable_stdio_uart(${bin_name} 1)

# Link necessary libraries
target_link_libraries(${bin_name} PRIVATE
    pico_stdlib
    pico_audio_32b
    pico_audio_i2s_32b
    hardware_pio       # Required for quadrature encoder PIO
    hardware_irq       # Required for handling encoder interrupts
    pico_multicore     # If using Core 1 for processing
)

# Generate PIO Header for Quadrature Encoder
pico_generate_pio_header(${bin_name} ${CMAKE_CURRENT_LIST_DIR}/pio/quadrature_encoder.pio)

# Add I2S and Core1 processing options
target_compile_definitions(${bin_name} PRIVATE
    PICO_AUDIO_I2S_PIO=1
    PICO_AUDIO_I2S_DMA_IRQ=1
    CORE1_PROCESS_I2S_CALLBACK
)

# Extra outputs
pico_add_extra_outputs(${bin_name})
