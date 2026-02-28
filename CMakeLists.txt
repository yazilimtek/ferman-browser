cmake_minimum_required(VERSION 3.20)
project(ferzan-browser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4    REQUIRED gtk4)
pkg_check_modules(WEBKIT  REQUIRED webkitgtk-6.0)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(SOUP    REQUIRED libsoup-3.0)
pkg_check_modules(POPPLER         poppler-glib)
pkg_check_modules(LIBZIP          libzip)

set(SOURCES
    src/main.cpp
    src/browser_window.cpp
    src/session_manager.cpp
    src/download_manager.cpp
    src/history_manager.cpp
    src/bookmark_manager.cpp
    src/settings_manager.cpp
    src/ai_manager.cpp
    src/file_extractor.cpp
)

add_executable(ferzan-browser ${SOURCES})

# poppler/libzip opsiyonel — kuruluysa etkinleştir
if(POPPLER_FOUND)
    target_compile_definitions(ferzan-browser PRIVATE HAVE_POPPLER)
    target_include_directories(ferzan-browser PRIVATE ${POPPLER_INCLUDE_DIRS})
    target_compile_options(ferzan-browser PRIVATE ${POPPLER_CFLAGS_OTHER})
endif()
if(LIBZIP_FOUND)
    target_compile_definitions(ferzan-browser PRIVATE HAVE_LIBZIP)
    target_include_directories(ferzan-browser PRIVATE ${LIBZIP_INCLUDE_DIRS})
    target_compile_options(ferzan-browser PRIVATE ${LIBZIP_CFLAGS_OTHER})
endif()

target_include_directories(ferzan-browser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GTK4_INCLUDE_DIRS}
    ${WEBKIT_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${SOUP_INCLUDE_DIRS}
)

target_compile_options(ferzan-browser PRIVATE
    ${GTK4_CFLAGS_OTHER}
    ${WEBKIT_CFLAGS_OTHER}
    ${SQLITE3_CFLAGS_OTHER}
    ${SOUP_CFLAGS_OTHER}
)

target_link_libraries(ferzan-browser
    ${GTK4_LIBRARIES}
    ${WEBKIT_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${SOUP_LIBRARIES}
    $<$<BOOL:${POPPLER_FOUND}>:${POPPLER_LIBRARIES}>
    $<$<BOOL:${LIBZIP_FOUND}>:${LIBZIP_LIBRARIES}>
)
