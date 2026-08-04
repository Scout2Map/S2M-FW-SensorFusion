# File   : CMakeLists.txt
# Purpose: Build configuration for the Scout2Map sensor fusion firmware
#          targeting the Raspberry Pi Pico 2 (RP2350).
# Author : jihoonkimtech

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include(pico_sdk_import.cmake)

project(scout2map_sensor_fusion C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(sensor_fusion
    main.c
    libs/queue/line_queue.c
    libs/diag/i2c_scan.c
    libs/drivers/aht21.c
    libs/drivers/ens160.c
    libs/drivers/bh1750.c
    libs/drivers/pms7003.c
)

# Headers are referenced as "drivers/xxx.h" and "queue/xxx.h"
target_include_directories(sensor_fusion PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/libs
)

target_link_libraries(sensor_fusion
    pico_stdlib
    pico_sync
    hardware_i2c
    hardware_uart
)

# Keep line endings as bare LF so the host parser sees clean JSON lines
target_compile_definitions(sensor_fusion PRIVATE
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
)

# Route stdio to USB CDC only.
# stdio_uart MUST stay off: it defaults to GP0/GP1, which the PMS7003 owns.
# Leaving it on would inject debug bytes into the sensor and override its 9600 baud.
pico_enable_stdio_usb(sensor_fusion 1)
pico_enable_stdio_uart(sensor_fusion 0)

pico_add_extra_outputs(sensor_fusion)
