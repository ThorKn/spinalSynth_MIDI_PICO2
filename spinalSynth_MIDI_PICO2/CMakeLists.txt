# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(spinalSynth_MIDI_PICO2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add Pico-PIO-USB subdirectory
add_subdirectory(Pico-PIO-USB)

# Add executable. Default name is the project name, version 0.1

add_executable(spinalSynth_MIDI_PICO2
    spinalSynth_MIDI_PICO2.c
    usb_midi_host.c
    hcd_pio_usb.c
)

pico_set_program_name(spinalSynth_MIDI_PICO2 "spinalSynth_MIDI_PICO2")
pico_set_program_version(spinalSynth_MIDI_PICO2 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(spinalSynth_MIDI_PICO2 1)
pico_enable_stdio_usb(spinalSynth_MIDI_PICO2 0)

# Add the standard library, TinyUSB Host, and PIO USB libraries to the build
target_link_libraries(spinalSynth_MIDI_PICO2
        pico_stdlib
        tinyusb_host
        pico_pio_usb
        m)

# Add the standard include files to the build
target_include_directories(spinalSynth_MIDI_PICO2 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Use TinyUSB's implementation for Pico-PIO-USB
target_compile_definitions(spinalSynth_MIDI_PICO2 PRIVATE 
    PIO_USB_USE_TINYUSB
    USB_DEBUG_PRINTS=0
)

pico_add_extra_outputs(spinalSynth_MIDI_PICO2)

