cmake_minimum_required(VERSION 3.10)

project(iolinki_master VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set(IOLINKI_DEVICE_DIR
    "/home/andrii/projects/labwired/core/third_party/iolinki"
    CACHE PATH "Path to the local iolinki device repository")

option(BUILD_TESTING "Build unit tests" ON)
set(IOLINKI_MASTER_BUILD_TESTING "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE BOOL "Build dependency unit tests" FORCE)
add_subdirectory("${IOLINKI_DEVICE_DIR}" "${CMAKE_BINARY_DIR}/iolinki-device" EXCLUDE_FROM_ALL)
set(BUILD_TESTING "${IOLINKI_MASTER_BUILD_TESTING}" CACHE BOOL "Build unit tests" FORCE)

add_library(iolinki_master STATIC src/master_port.c)
target_include_directories(iolinki_master PUBLIC include "${IOLINKI_DEVICE_DIR}/include")
target_link_libraries(iolinki_master PUBLIC iolinki)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
