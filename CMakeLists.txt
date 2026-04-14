cmake_minimum_required(VERSION 3.13)

# When cross-compiling on Windows, prevent CMake from trying to build host
# executables during try_compile checks (use static library instead).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# Set Pico board to Pico W 2 (RP2350-based)
set(PICO_BOARD pico2)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Import the Pico SDK early so CMake can configure the cross toolchain
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
project(${project_name} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_FLASH_SIZE_BYTES 4194304)


#set(FAMILY rp2350)
#set(BOARD pico2)


set(CONFIG_I2S_DATA_PIN 14)
set(CONFIG_I2S_CLOCK_PIN_BASE 12)

pico_sdk_init()

# Add I2S Audio Libraries
# add_subdirectory(lib/pico_audio_i2s_32b)
# add_subdirectory(lib/pico_audio_i2s_32b/src/pico_audio_32b)
# add_subdirectory(lib/fpm)
# add_subdirectory(lib/pico-ssd1306)
add_subdirectory(lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src)

set(bin_name ${PROJECT_NAME})
add_executable(${bin_name}
    src/main.cpp
)

target_link_options(${bin_name} PRIVATE
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${bin_name}.map"
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
    -Wno-error=shadow

    #these are for the sd-card lib
    -Wno-error=cast-qual
    -Wno-error=float-conversion
    -Wno-error=float-equal
)


target_compile_definitions(pico-synth PRIVATE
	PICO_AUDIO_I2S_DATA_PIN=${CONFIG_I2S_DATA_PIN}
	PICO_AUDIO_I2S_CLOCK_PIN_BASE=${CONFIG_I2S_CLOCK_PIN_BASE}
)

target_sources(${bin_name} PRIVATE 

    src/MidiHandler.cpp
    src/HardwareManager.cpp
    src/Sequencer.cpp
    src/Sampler.cpp
    src/Preset.cpp

    src/synth/Wavetable.cpp
    src/synth/Oscillator.cpp
    src/synth/Envelope.cpp
    src/synth/Synth.cpp
    src/synth/Operator.cpp
    src/synth/KarplusStrong.cpp
    src/synth/Modal.cpp
    src/synth/Filter.cpp
    src/synth/Delay.cpp
    src/synth/Compressor.cpp
    src/synth/Distortion.cpp
    src/synth/Reverb.cpp
    src/synth/ReverbSc.cpp
    src/synth/Chorus.cpp

    src/ui/Ui.cpp
    src/ui/main_menu.cpp
    src/ui/midi_menu.cpp
    src/ui/sequencer_menu.cpp
    src/ui/sequencer_edit_menu.cpp
    src/ui/choose_menu.cpp
    src/ui/engine_select_menu.cpp
    src/ui/sampler_menu.cpp
    src/ui/draw_utils.cpp
    src/ui/fm_menu.cpp
    src/ui/karplus_menu.cpp
    src/ui/modal_menu.cpp
    src/ui/fx_menu.cpp
    src/ui/analog_menu.cpp
    src/ui/lfo_menu.cpp


    src/audio.c
    src/usb_descriptors.c
    src/tusb_config.h
    src/ssd1306.c
    src/hw_config.c
    src/msc_disk.c
)

# Add include directories for TinyUSB
target_include_directories(pico-synth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/synth
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/tinyusb/hw
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/tinyusb
)

# Enable USB and UART for debugging
pico_enable_stdio_usb(${bin_name} 1)
pico_enable_stdio_uart(${bin_name} 0)

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
    tinyusb_additions
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
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

add_custom_command(TARGET ${bin_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DUF2_PATH=$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/${bin_name}.uf2>
        -DBIN_PATH=$<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/${bin_name}.bin>
        -DFLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES}
        -P ${CMAKE_CURRENT_LIST_DIR}/cmake/print_pico_image_size.cmake
    VERBATIM
)
