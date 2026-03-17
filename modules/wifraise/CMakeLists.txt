add_library(wifraise INTERFACE)

target_sources(wifraise INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fraise.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ota_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dhcpserver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dnsserver.c
)

target_include_directories(wifraise INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(wifraise INTERFACE
    pico_cyw43_arch_lwip_poll
    pico_stdlib
    pico_sha256
    pico_multicore
    boot_uf2_headers
)

