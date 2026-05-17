# mbedTLS CMakeLists.txt

set(MBEDTLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stm32-mw-mbedtls)

# Use globbing to find all .c files in library/
file(GLOB MBEDTLS_LIB_SOURCES "${MBEDTLS_DIR}/library/*.c")

# Exclude files not suitable for embedded platforms
list(REMOVE_ITEM MBEDTLS_LIB_SOURCES "${MBEDTLS_DIR}/library/net_sockets.c")
list(REMOVE_ITEM MBEDTLS_LIB_SOURCES "${MBEDTLS_DIR}/library/timing.c")

set(MBEDTLS_SOURCES
    ${MBEDTLS_LIB_SOURCES}
    
    # User Port / ALT
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/aes_alt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/low_level_rng.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/rsa_alt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/sha256_alt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/ecdsa_alt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/ecp_alt.c
)

# Create a separate static library for mbedTLS
add_library(mbedtls STATIC ${MBEDTLS_SOURCES})

# Public include directories (propagated to targets that link to this)
target_include_directories(mbedtls PUBLIC
    ${MBEDTLS_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Target
    
    # STM32 HAL and CMSIS includes (relative to this file)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Drivers/STM32H5xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Drivers/CMSIS/Device/ST/STM32H5xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Drivers/CMSIS/Include
)

target_include_directories(mbedtls PRIVATE
    ${MBEDTLS_DIR}/library
)

# Public definitions
target_compile_definitions(mbedtls PUBLIC
    STM32H573xx
    MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Target/mbedtls_config.h"
    MBEDTLS_PLATFORM_MS_TIME_ALT
    MBEDTLS_HAVE_TIME
)

# Set optimization to -O2 for mbedTLS to fix "not enough registers" error in bignum_core.c
target_compile_options(mbedtls PRIVATE "-O2")

# Link the custom mbedTLS library to the main project (using plain signature to match CubeMX)
target_link_libraries(${CMAKE_PROJECT_NAME} mbedtls)
