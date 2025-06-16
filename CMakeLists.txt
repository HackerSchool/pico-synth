cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)



# If no build type specified, default to Release (max optimization)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Explicitly set optimization flags for Release builds
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

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


set(CONFIG_I2S_DATA_PIN 14)
set(CONFIG_I2S_CLOCK_PIN_BASE 12)

pico_sdk_init()

# Add I2S Audio Libraries
# add_subdirectory(lib/pico_audio_i2s_32b)
# add_subdirectory(lib/pico_audio_i2s_32b/src/pico_audio_32b)
# add_subdirectory(lib/fpm)
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


target_compile_definitions(pico-synth PRIVATE
	PICO_AUDIO_I2S_DATA_PIN=${CONFIG_I2S_DATA_PIN}
	PICO_AUDIO_I2S_CLOCK_PIN_BASE=${CONFIG_I2S_CLOCK_PIN_BASE}
)

target_sources(${bin_name} PRIVATE 
    # src/i2s_init.cpp
    src/audio.c
    src/Sequencer.cpp
    src/synth/Wavetable.cpp
    src/synth/Oscillator.cpp
    src/synth/Envelope.cpp
    src/synth/Synth.cpp
    src/ui/Ui.cpp
    src/ui/main_menu.cpp
    src/ui/midi_menu.cpp
    src/ui/sequencer_menu.cpp
    src/ui/sequencer_edit_menu.cpp
    src/MidiHandler.cpp
    src/synth/Filter.cpp
    src/HardwareManager.cpp
    src/usb_descriptors.c
    src/tusb_config.h
    src/ssd1306.c
)

# Add include directories for TinyUSB
target_include_directories(pico-synth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/synth
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

# Enable USB and UART for debugging
pico_enable_stdio_usb(${bin_name} 1)
pico_enable_stdio_uart(${bin_name} 1)

# Link necessary libraries
target_link_libraries(${bin_name} PRIVATE
    pico_stdlib
    # pico_audio_32b
    # pico_audio_i2s_32b
    pico_audio_i2s
    hardware_pio       # Required for quadrature encoder PIO
    hardware_irq       # Required for handling encoder interrupts
    hardware_i2c       # Required for i2c
    hardware_interp       # Required for i2c
    pico_multicore     # If using Core 1 for processing
    tinyusb_device
    tinyusb_board
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
