cmake_minimum_required(VERSION 3.20)
project(ferzan-browser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(WEBKIT REQUIRED webkitgtk-6.0)

set(SOURCES
    src/main.cpp
    src/browser_window.cpp
)

add_executable(ferzan-browser ${SOURCES})

target_include_directories(ferzan-browser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GTK4_INCLUDE_DIRS}
    ${WEBKIT_INCLUDE_DIRS}
)

target_compile_options(ferzan-browser PRIVATE
    ${GTK4_CFLAGS_OTHER}
    ${WEBKIT_CFLAGS_OTHER}
)

target_link_libraries(ferzan-browser
    ${GTK4_LIBRARIES}
    ${WEBKIT_LIBRARIES}
)
