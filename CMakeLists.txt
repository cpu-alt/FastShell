cmake_minimum_required(VERSION 3.21)

project(FastShell
    VERSION 1.0.0
    DESCRIPTION "FastShell: SSH, Telnet, serial, RDP and local terminal"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FASTSHELL_BUILD_TESTS "Build unit tests" ON)
option(FASTSHELL_ENABLE_LTO  "Enable link-time optimization in release builds" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network SerialPort)
find_package(OpenSSL 3 REQUIRED)
find_package(libssh CONFIG REQUIRED)

if(FASTSHELL_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg LANGUAGES CXX)
    if(ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# Shared warning/optimization flags for every target in the project.
add_library(fastshell_options INTERFACE)
if(MSVC)
    target_compile_options(fastshell_options INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(fastshell_options INTERFACE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()
target_compile_definitions(fastshell_options INTERFACE
    QT_NO_KEYWORDS
    QT_DISABLE_DEPRECATED_UP_TO=0x060500)
if(WIN32)
    # ConPty needs the Windows 10 1809 SDK definitions.
    target_compile_definitions(fastshell_options INTERFACE
        _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006 WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

add_subdirectory(src)

if(FASTSHELL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
