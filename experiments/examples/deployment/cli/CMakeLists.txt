cmake_minimum_required(VERSION 3.16)
project(qhufdel_cli C)

# The CLI reuses the decoder and CRC-32 libraries from the repository root.
#   src/                    CLI, model loader, host port of the benchmark
#   ../../../../decoder     Q-HUFDEL decoder (same sources as the firmware)
#   ../../../../third-party CRC-32
get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(qhufdel_cli
    src/main.c
    src/bench_timer.c
    src/model_loader.c
    src/package_runner.c
    ${REPOSITORY_ROOT}/decoder/src/decoder.c
    ${REPOSITORY_ROOT}/decoder/src/bit_manager.c
    ${REPOSITORY_ROOT}/third-party/crc32/src/crc32.c
)

target_include_directories(qhufdel_cli PRIVATE
    src
    ${REPOSITORY_ROOT}/decoder/include
    ${REPOSITORY_ROOT}/third-party/crc32/inc
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qhufdel_cli PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Standalone .exe: no libgcc / libwinpthread DLLs needed next to it
if(MINGW)
    target_link_options(qhufdel_cli PRIVATE -static)
endif()

# Copy the executable to bin/ after each build
add_custom_command(TARGET qhufdel_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:qhufdel_cli> ${CMAKE_CURRENT_SOURCE_DIR}/bin/
)
