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
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(nisepac2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(nisepac2 nisepac2.c )

target_sources(nisepac2 PRIVATE nisepac2.c hid_app.c joystick.c hidparser/hidparser.c)

pico_set_program_name(nisepac2 "nisepac2")
pico_set_program_version(nisepac2 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(nisepac2 1)
pico_enable_stdio_usb(nisepac2 0)

# Add the standard library to the build
target_link_libraries(nisepac2  
        hardware_flash hardware_vreg pico_multicore tinyusb_host tinyusb_board pico_stdlib)

# Add the standard include files to the build
target_include_directories(nisepac2 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

#pico_set_binary_type(nisepac2 no_flash)

pico_add_extra_outputs(nisepac2)

