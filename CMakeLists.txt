cmake_minimum_required(VERSION 3.13)

# min supported version macOS High Sierra
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")

# Phoscon App and WebApp 2016 are fetched as external zip files
# and are then copied in the respective packages.
set(PWA_URL http://deconz.dresden-elektronik.de/phoscon_app/phoscon_app_2024-09-27.zip)
set(WEBAPP_URL http://deconz.dresden-elektronik.de/phoscon_app/webapp_2016.zip)

project(deCONZ VERSION 2.29.5)

if (NOT DEFINED BUILD_CHANNEL)
    set(BUILD_CHANNEL "")
endif()

include(FetchContent)
include(GNUInstallDirs)

set(CMAKE_MACOSX_BUNDLE ON)

add_compile_options(-Wno-psabi)

find_package(Git REQUIRED)

# --- reproducable builds via SOURCE_DATE_EPOCH -----------------------

if (Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} show -s --format=%ct
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_SRC_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

set(ENV{SOURCE_DATE_EPOCH} ${GIT_SRC_DATE})

# --- build SQLite -----------------------------------------
set(DECONZ_FULL_BUILD ON)
add_subdirectory(lib/sqlite3)

add_subdirectory(src)

# --- build GCFFlasher_internal ----------------------------
if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    add_subdirectory(src/3rdparty/gcfflasher)
    set_target_properties( GCFFlasher PROPERTIES OUTPUT_NAME "GCFFlasher_internal" )
endif()

# --- packageing ------------------------------------------------------

if (APPLE)
    set(PWA_DESTINATION "src/deCONZ.app/Contents/Resources/webapp")
endif()

if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    set(PWA_DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/deCONZ/webapp")

    install(PROGRAMS "${CMAKE_BINARY_DIR}/src/3rdparty/gcfflasher/GCFFlasher_internal" DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME "GCFFlasher_internal.bin")

    install(DIRECTORY "linux/deCONZ/lib/systemd" DESTINATION "/lib")
    install(FILES "linux/deCONZ/usr/share/applications/deCONZ.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES "linux/deCONZ/usr/share/deCONZ/doc/deCONZ-BHB-en.pdf" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/deCONZ/doc")
    install(FILES "src/icons/de_logo_48px.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/deCONZ/icons")

    install(
            DIRECTORY "linux/deCONZ/usr/bin/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING
            PATTERN "*.sh"
        )
endif()

set(PWA_PATH ${CMAKE_BINARY_DIR}/pwa.zip)
set(WEBAPP_PATH ${CMAKE_BINARY_DIR}/webapp_2016.zip)

file(DOWNLOAD ${WEBAPP_URL} ${WEBAPP_PATH} INACTIVITY_TIMEOUT 30)
file(ARCHIVE_EXTRACT INPUT ${WEBAPP_PATH})

file(DOWNLOAD ${PWA_URL} ${PWA_PATH} INACTIVITY_TIMEOUT 30)
file(ARCHIVE_EXTRACT INPUT ${PWA_PATH})

if (PWA_DESTINATION)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/webapp_2016/"
        DESTINATION "${PWA_DESTINATION}")

    install(DIRECTORY "${CMAKE_BINARY_DIR}/pwa/"
        DESTINATION "${PWA_DESTINATION}/pwa")
endif()

# --- Debian package --------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Manuel Pietschmann <mpi@dresden-elektronik.de>")
set(CPACK_DEBIAN_PACKAGE_SECTION "non-free / misc")

set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt5core5a, libqt5network5, libqt5widgets5, libqt5gui5, libqt5serialport5, libqt5websockets5, libqt5sql5, libqt5qml5, libcap2-bin, sqlite3, lsof, curl")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "udev")

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/linux/deCONZ/DEBIAN/postinst;${CMAKE_CURRENT_SOURCE_DIR}/linux/deCONZ/DEBIAN/preinst")

# --- common-----------------------------------------------------------

string(TOLOWER "${CMAKE_SYSTEM_NAME}" LOWERCASE_SYSTEM_NAME)

# keep arch names as used in the past
set(PKG_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if (${PKG_ARCH} MATCHES "aarch64")
    set(PKG_ARCH "arm64")
endif()
if (${PKG_ARCH} MATCHES "armv7l")
    set(PKG_ARCH "armhf")
endif()
if (${PKG_ARCH} MATCHES "x86_64")
    set(PKG_ARCH "amd64")
endif()

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/LICENSE.txt")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://phoscon.de")
set(CPACK_PACKAGE_DESCRIPTION "Zigbee monitoring and control.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION}${BUILD_CHANNEL})
if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    set(CPACK_PACKAGE_FILE_NAME "deconz_${PROJECT_VERSION}-debian${BUILD_CHANNEL}_${PKG_ARCH}")
else()
    set(CPACK_PACKAGE_FILE_NAME "deconz_${PROJECT_VERSION}_${LOWERCASE_SYSTEM_NAME}_${PKG_ARCH}")
endif()

include(CPack)
