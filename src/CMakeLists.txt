cmake_minimum_required(VERSION 3.13)


option(USE_QT6 "Build with Qt6" OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(USE_QT6)
  set(QT_VERSION_MAJOR 6)
  add_compile_definitions(QT_NO_DEPRECATED_WARNINGS=1)
elseif(CMAKE_PREFIX_PATH MATCHES "5\.[0-9][0-9]\.[0-9]")
  set(QT_VERSION_MAJOR 5)
elseif(CMAKE_PREFIX_PATH MATCHES "6\.[0-9][0-9]\.[0-9]")
  set(QT_VERSION_MAJOR 6)
  set(USE_QT6 1)
else()
  set(QT_VERSION_MAJOR 5) # default to qt5
endif()

if (QT_VERSION_MAJOR EQUAL 6)
  find_package(Qt6 COMPONENTS Core Gui Widgets SerialPort Network REQUIRED)
elseif (QT_VERSION_MAJOR EQUAL 5)
  find_package(Qt5 COMPONENTS Core Gui Widgets SerialPort Network REQUIRED)
else()
    message( FATAL_ERROR "QT_VERSION_MAJOR must be defined" )
endif()

set(PUB_INCLUDE_FILES
    3rdparty/actor_model/actor/plugin.h
    3rdparty/actor_model/actor/cxx_helper.h

    actor_vfs_model.h
    common/zm_protocol.h
    deconz/net_descriptor.h
    deconz/security_key.h
    deconz/util_private.h
    db_json_nodes.h
    db_nodes.h
    debug_view.h
    gui/actor_vfs_view.h
    gui/theme.h
    mainwindow.h
    send_to_dialog.h
    source_route_info.h
    source_routing.h
    zm_about_dialog.h
    zm_app.h
    zm_attribute_info.h
    zm_binddropbox.h
    zm_cluster_info.h
    zm_command_info.h
    zm_controller.h
    zm_gcheckbox.h
    zm_gcluster.h
    zm_gendpoint.h
    zm_gendpointbox.h
    zm_glink.h
    zm_global.h
    zm_gnode.h
    zm_graphicsview.h
    zm_gsocket.h
    zm_gsourceroute.h
    zm_http_client.h
    zm_http_server.h
    zm_master.h
    zm_master_com.h
    zm_neighbor.h
    zm_netdescriptor_model.h
    zm_netedit.h
    zm_node.h
    zm_node_info.h
    zm_node_model.h
    zm_settings_dialog.h
    zm_settings_discovery.h
    zm_settings_zcldb.h
)

add_executable(${PROJECT_NAME}
    ${PUB_INCLUDE_FILES}

    zm_resources.qrc

    3rdparty/actor_model/am_service/am_service.c
    3rdparty/actor_model/am_service/am_plugin_loader.c
    3rdparty/actor_model/am_service/am_message_rw.c

    debug_view.ui
    gui/actor_vfs_view.ui
    mainwindow.ui
    send_to_dialog.ui
    source_route_info.ui
    zm_about_dialog.ui
    zm_attribute_info.ui
    zm_binddropbox.ui
    zm_cluster_info.ui
    zm_command_info.ui
    zm_netedit.ui
    zm_node_info.ui
    zm_settings_dialog.ui
    zm_settings_discovery.ui
    zm_settings_zcldb.ui

    actor_vfs_model.cpp
    common/protocol.c
    common/zm_protocol.c
    db_json_nodes.cpp
    db_nodes.cpp
    debug_view.cpp
    gui/actor_vfs_view.cpp
    gui/theme.cpp
    main.cpp
    mainwindow.cpp
    send_to_dialog.cpp
    source_route_info.cpp
    source_routing.cpp
    util_private.cpp
    zm_about_dialog.cpp
    zm_app.cpp
    zm_attribute_info.cpp
    zm_binddropbox.cpp
    zm_cluster_info.cpp
    zm_command_info.cpp
    zm_controller.cpp
    zm_gcheckbox.cpp
    zm_gcluster.cpp
    zm_gendpoint.cpp
    zm_gendpointbox.cpp
    zm_glink.cpp
    zm_gnode.cpp
    zm_graphicsview.cpp
    zm_gsocket.cpp
    zm_gsourceroute.cpp
    zm_http_client.cpp
    zm_http_server.cpp
    zm_master.cpp
    zm_master_com_serial.cpp
    zm_neighbor.cpp
    zm_netdescriptor_model.cpp
    zm_netedit.cpp
    zm_node.cpp
    zm_node_info.cpp
    zm_node_model.cpp
    zm_settings_dialog.cpp
    zm_settings_discovery.cpp
    zm_settings_zcldb.cpp
    gui/gnode_link_group.h gui/gnode_link_group.cpp
)

if (WIN32)
    if(MINGW)
        # This gets us DLL resource information when compiling on MinGW.
        #if(NOT CMAKE_RC_COMPILER)
            set(CMAKE_RC_COMPILER windres.exe)
        #endif()
    endif()
    target_sources(${PROJECT_NAME} PRIVATE app.rc)
    target_sources(${PROJECT_NAME} PRIVATE applink.c)
endif()

# deCONZ library
add_subdirectory(lib)

add_library(am_plugin_hdr INTERFACE)
target_include_directories(am_plugin_hdr INTERFACE 3rdparty/actor_model)

set_target_properties(${PROJECT_NAME} PROPERTIES
    AUTOMOC ON
)

#set_target_properties(${PROJECT_NAME} PROPERTIES
#    MACOSX_BUNDLE TRUE
#    MACOSX_BUNDLE_GUI_IDENTIFIER de.phoscon.deconz
#    MACOSX_BUNDLE_BUNDLE_NAME deCONZ
#    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/bundle.plist.in)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_14)

target_compile_definitions(${PROJECT_NAME} PUBLIC "$<$<CONFIG:Debug>:DECONZ_DEBUG_BUILD>")


if (Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%at
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list HEAD --max-count=1
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        OUTPUT_VARIABLE GIT_TAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string (REPLACE "\n" ";" GIT_TAGS "${GIT_TAGS}")
endif()

message(STATUS "build channel: ${BUILD_CHANNEL}, tags: #${GIT_TAGS}#")

configure_file(config.h.in config.h @ONLY)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    APP_VERSION_BUGFIX=${PROJECT_VERSION_PATCH}
    APP_CHANNEL="\"\"${BUILD_CHANNEL}\"\""
    APP_MIN_NODES=5
    APP_MAX_NODES=512
    APP_FEATURE_SOURCE_ROUTING
    COMMERCIAL_KEY=0x93UL # RaspBee, ConBee, ConBee II, RaspBee II
    USE_QEXT_SERIAL
    USE_ULIB_SHARED
    USE_ACTOR_MODEL
    AM_DEFAULT_RESPONSE_TIMEOUT=10000
    AM_SERVICE_VERSION_MAJOR=1
    AM_SERVICE_VERSION_MINOR=0
    AM_SERVICE_VERSION_PATCH=0
)

# keep arch names as used in the past
set(SYS_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if (${SYS_ARCH} MATCHES "aarch64")
    target_compile_definitions(${PROJECT_NAME} PRIVATE   ARCH_ARM ARCH_ARM64)
elseif (${SYS_ARCH} MATCHES "^arm")
    target_compile_definitions(${PROJECT_NAME} PRIVATE   ARCH_ARM)
endif()
if (${SYS_ARCH} MATCHES "x86_64")
    target_compile_definitions(${PROJECT_NAME} PRIVATE   ARCH_AMD64)
endif()


if (APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PL_MACOS PL_UNIX)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE PL_LINUX PL_UNIX)
endif()

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PL_WINDOWS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_QSERIAL_PORT)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE Qt${QT_VERSION_MAJOR}::Core
    PRIVATE Qt${QT_VERSION_MAJOR}::Gui
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    PRIVATE Qt${QT_VERSION_MAJOR}::SerialPort
    PRIVATE Qt${QT_VERSION_MAJOR}::Network
    PRIVATE SQLite::SQLite3
    PRIVATE deCONZLib
    PRIVATE am_plugin_hdr
    PRIVATE pthread
)

if (UNIX)
    if (APPLE)
        set_target_properties(deCONZLib PROPERTIES
            INSTALL_RPATH @loader_path)

        set_target_properties(${PROJECT_NAME} PROPERTIES
            BUNDLE True
            OUTPUT_NAME "deCONZ"
            EXECUTABLE_OUTPUT_PATH ".."
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/../Info.plist
            INSTALL_RPATH "@executable_path/../Frameworks")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib")
    endif()
endif()

#--------------------------------------------------------------
add_subdirectory(plugins)

#--------------------------------------------------------------
include(GNUInstallDirs)


if (APPLE)
    install(TARGETS deCONZ deCONZLib deconzsqlite3
        LIBRARY DESTINATION deCONZ.app/Contents/Frameworks
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
    )

    install(TARGETS de_rest_plugin
        LIBRARY DESTINATION deCONZ.app/Contents/Plugins
    )

    install(TARGETS std_otau_plugin
        LIBRARY DESTINATION deCONZ.app/Contents/Plugins
    )

    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../icons/deconz.icns"
        DESTINATION deCONZ.app/Contents/Resources)

    install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/plugins/de_web/devices"
        DESTINATION deCONZ.app/Contents/Resources)

    install(FILES "${CMAKE_CURRENT_LIST_DIR}/plugins/de_web/general.xml"
        DESTINATION deCONZ.app/Contents/Resources/zcl)

    install(FILES "${CMAKE_CURRENT_LIST_DIR}/plugins/de_web/button_maps.json"
        DESTINATION deCONZ.app/Contents/Resources/devices)
endif()

if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    install(TARGETS deCONZ deCONZLib deconzsqlite3
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if (WIN32 AND CMAKE_HOST_WIN32)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME_DEPENDENCIES
        DIRECTORIES lib C:/msys64/mingw32/bin
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-" ".*deconz.*"
        POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()


if (WIN32 AND CMAKE_HOST_WIN32)
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.exe DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME deCONZ.exe)
    install(PROGRAMS C:/msys64/mingw32/share/qt5/plugins/platforms/qwindows.dll DESTINATION ${CMAKE_INSTALL_BINDIR}/platforms)
else()
    if (NOT APPLE)
        install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME deCONZ)
    endif()
endif()
