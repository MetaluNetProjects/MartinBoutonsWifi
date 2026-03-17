cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico2_w)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico2_ota C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

include(wifi.cmake)

add_compile_definitions(MAX_NUM_PIXELS=122)
string(APPEND CMAKE_EXE_LINKER_FLAGS "-Wl,--print-memory-usage")

if (DEFINED ENV{WIFI_SSID} AND (NOT WIFI_SSID))
    set(WIFI_SSID $ENV{WIFI_SSID})
    message("Using WIFI_SSID from environment ('${WIFI_SSID}')")
endif()

if (DEFINED ENV{WIFI_PASSWORD} AND (NOT WIFI_PASSWORD))
    set(WIFI_PASSWORD $ENV{WIFI_PASSWORD})
    message("Using WIFI_PASSWORD from environment")
endif()

if ("${WIFI_SSID}" STREQUAL "")
    message(FATAL_ERROR "WIFI_SSID is not defined")
elseif ("${WIFI_PASSWORD}" STREQUAL "")
    message(FATAL_ERROR "WIFI_PASSWORD is not defined")
endif()

add_executable(${CMAKE_PROJECT_NAME}
        main.cpp
        system.cpp
        version.cpp
        fraise.c
        ${CMAKE_CURRENT_LIST_DIR}/../modules/ota_server/ota_server.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../modules/dhcpserver/dhcpserver.c
        ${CMAKE_CURRENT_LIST_DIR}/../modules/dnsserver/dnsserver.c
        )
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        WIFI_SSID=\"${WIFI_SSID}\"
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        PICO_CRT0_IMAGE_TYPE_TBYB=1
        )
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
        ${CMAKE_CURRENT_LIST_DIR}/../modules/dhcpserver
        ${CMAKE_CURRENT_LIST_DIR}/../modules/dnsserver
        ${CMAKE_CURRENT_LIST_DIR}/../modules/ota_server
        )

add_subdirectory(../modules modules)

target_link_libraries(${CMAKE_PROJECT_NAME}
        #pico_cyw43_arch_lwip_threadsafe_background
        pico_cyw43_arch_lwip_poll
        pico_stdlib
        pico_sha256
        pico_multicore
        boot_uf2_headers
        hardware_pwm
        ws2812par
        )

# force recompile version.cpp each time for updating __TIME__
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp
        ) 

pico_use_wifi_firmware_partition(${CMAKE_PROJECT_NAME})

pico_hash_binary(${CMAKE_PROJECT_NAME})
pico_sign_binary(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/private.pem)

# By default this example requires a partition table in flash, and will
# update the partition that is not currently in use. To use it without
# a partition table in flash, uncomment the following lines to make it
# a no_flash binary, so it can update the currently running program.

# pico_set_binary_type(${CMAKE_PROJECT_NAME} no_flash)
# pico_package_uf2_output(${CMAKE_PROJECT_NAME} 0x10000000)

pico_add_extra_outputs(${CMAKE_PROJECT_NAME})
