cmake_minimum_required(VERSION 3.20)
project(ferman-browser LANGUAGES CXX)

set(APP_VERSION "1.0.0")
set(APP_NAME    "Ferman Browser")
set(APP_BINARY  "ferman-browser")

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
    src/crypto_utils.cpp
    src/setup_manager.cpp
)

add_executable(ferman-browser ${SOURCES})
set_target_properties(ferman-browser PROPERTIES OUTPUT_NAME "ferman-browser")

# poppler/libzip opsiyonel — kuruluysa etkinleştir
if(POPPLER_FOUND)
    target_compile_definitions(ferman-browser PRIVATE HAVE_POPPLER)
    target_include_directories(ferman-browser PRIVATE ${POPPLER_INCLUDE_DIRS})
    target_compile_options(ferman-browser PRIVATE ${POPPLER_CFLAGS_OTHER})
endif()
if(LIBZIP_FOUND)
    target_compile_definitions(ferman-browser PRIVATE HAVE_LIBZIP)
    target_include_directories(ferman-browser PRIVATE ${LIBZIP_INCLUDE_DIRS})
    target_compile_options(ferman-browser PRIVATE ${LIBZIP_CFLAGS_OTHER})
endif()

target_include_directories(ferman-browser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GTK4_INCLUDE_DIRS}
    ${WEBKIT_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${SOUP_INCLUDE_DIRS}
)

target_compile_options(ferman-browser PRIVATE
    ${GTK4_CFLAGS_OTHER}
    ${WEBKIT_CFLAGS_OTHER}
    ${SQLITE3_CFLAGS_OTHER}
    ${SOUP_CFLAGS_OTHER}
)

target_link_libraries(ferman-browser
    ${GTK4_LIBRARIES}
    ${WEBKIT_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${SOUP_LIBRARIES}
    $<$<BOOL:${POPPLER_FOUND}>:${POPPLER_LIBRARIES}>
    $<$<BOOL:${LIBZIP_FOUND}>:${LIBZIP_LIBRARIES}>
)

# ── Kurulum hedefleri ──────────────────────────────────────────────
install(TARGETS ferman-browser DESTINATION bin)

install(FILES resources/icons/16x16/apps/ferman-browser.png
        DESTINATION share/icons/hicolor/16x16/apps)
install(FILES resources/icons/32x32/apps/ferman-browser.png
        DESTINATION share/icons/hicolor/32x32/apps)
install(FILES resources/icons/192x192/apps/ferman-browser.png
        DESTINATION share/icons/hicolor/192x192/apps)
install(FILES resources/icons/512x512/apps/ferman-browser.png
        DESTINATION share/icons/hicolor/512x512/apps)

install(FILES resources/ferman-browser.desktop
        DESTINATION share/applications)

# ── CPack — .deb ve .tar.gz paketleri ────────────────────────────────
set(CPACK_PACKAGE_NAME        "ferman-browser")
set(CPACK_PACKAGE_VERSION     "${APP_VERSION}")
set(CPACK_PACKAGE_VENDOR      "Ferman")
set(CPACK_PACKAGE_DESCRIPTION "Ferman Browser - Modern Linux Web Tarayıcısı")
set(CPACK_PACKAGE_CONTACT     "ferman@ferman.net.tr")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# .deb
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libgtk-4-1, libwebkitgtk-6.0-4, libsqlite3-0, libsoup-3.0-0")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

# .tar.gz
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-linux-amd64")

include(CPack)
