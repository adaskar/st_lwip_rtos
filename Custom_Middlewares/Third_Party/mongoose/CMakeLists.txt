# Mongoose CMake configuration

set(MONGOOSE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mongoose)
set(LWIP_PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lwip)
set(LWIP_DIR ${LWIP_PORT_DIR}/stm32-mw-lwip)

add_library(mongoose STATIC
    ${MONGOOSE_DIR}/mongoose.c
)

target_include_directories(mongoose PUBLIC
    ${MONGOOSE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Target
)

target_include_directories(mongoose PRIVATE
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/system
    ${LWIP_PORT_DIR}/Target
)

target_link_libraries(mongoose PUBLIC
    mbedtls
    stm32cubemx
)

# Keep the single-file Mongoose implementation from inflating the debug build.
target_compile_options(mongoose PRIVATE "-O2")

target_link_libraries(${CMAKE_PROJECT_NAME} mongoose)
