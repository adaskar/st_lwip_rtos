# LibOSDP CMake configuration

set(LIBOSDP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libosdp)

set(OPT_BUILD_STATIC ON CACHE BOOL "Build libosdp static library" FORCE)
set(OPT_BUILD_SHARED OFF CACHE BOOL "Build libosdp shared library" FORCE)
set(OPT_OSDP_LIB_ONLY ON CACHE BOOL "Only build libosdp library" FORCE)
set(OPT_BUILD_BARE_METAL ON CACHE BOOL "Build libosdp for bare metal targets" FORCE)
set(OPT_USE_32BIT_TICK_T ON CACHE BOOL "Use uint32_t tick_t in libosdp" FORCE)
set(OPT_OSDP_LOG_MINIMAL ON CACHE BOOL "Minimize libosdp logger RAM/stack usage" FORCE)
set(OPT_OSDP_CRYPTO_BACKEND "tinyaes" CACHE STRING "libosdp crypto backend" FORCE)

add_subdirectory(${LIBOSDP_DIR})

target_link_libraries(${CMAKE_PROJECT_NAME} osdpstatic)
