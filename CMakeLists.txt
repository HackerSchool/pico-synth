cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)


# Disable Werror if it's being set somewhere else
if(CMAKE_CXX_FLAGS MATCHES "-Werror")
    string(REPLACE "-Werror" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

set(project_name "pico-synth")
project(${project_name})
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(FAMILY rp2040)
set(BOARD pico_sdk)

pico_sdk_init()

# Add I2S Audio Libraries
add_subdirectory(lib/pico_audio_i2s_32b)
add_subdirectory(lib/pico_audio_i2s_32b/src/pico_audio_32b)
add_subdirectory(lib/fpm)
# add_subdirectory(lib/pico-ssd1306)

set(bin_name ${PROJECT_NAME})
add_executable(${bin_name}
    src/main.cpp
)

# Some anoying erros I need to not show up
target_compile_options(pico-synth PRIVATE 
    -Wno-error=redundant-decls
    -Wno-error=conversion
    -Wno-error=sign-conversion
    -Wno-error=double-promotion
    -Wno-error=reorder
    -Wno-error=cast-align
    -Wno-error=missing-field-initializers
    -Wno-error=unused-but-set-variable
    -Wno-error=return-type
    -Wno-error=maybe-uninitialized
    -Wno-error=unused-parameter
    -Wno-error=discarded-qualifiers
)

target_sources(${bin_name} PRIVATE 
    src/i2s_init.cpp
    src/Wavetable.cpp
    src/Oscillator.cpp
    src/Envelope.cpp
    src/Synth.cpp
    src/MidiHandler.cpp
    src/Filter.cpp
    src/HardwareManager.cpp
    src/usb_descriptors.c
    src/tusb_config.h
    src/ssd1306.c
)

# Add include directories for TinyUSB
target_include_directories(pico-synth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    hardware_i2c       # Required for the screen
    pico_multicore     # If using Core 1 for processing
    tinyusb_device
    tinyusb_board
    fpm
)


family_configure_device_example(${bin_name} noos)

# Generate PIO Header for Quadrature Encoder
pico_generate_pio_header(${bin_name} ${CMAKE_CURRENT_LIST_DIR}/pio/quadrature_encoder.pio)

# Add I2S and Core1 processing options
target_compile_definitions(${bin_name} PRIVATE
    PICO_AUDIO_I2S_PIO=1
    PICO_AUDIO_I2S_DMA_IRQ=1
    # CORE1_PROCESS_I2S_CALLBACK
)

# Extra outputs
pico_add_extra_outputs(${bin_name})
