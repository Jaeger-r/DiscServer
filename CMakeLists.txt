if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(DiskServerStandalone LANGUAGES C CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_INCLUDE_CURRENT_DIR ON)

    if(APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target" FORCE)
        if(NOT CMAKE_OSX_SYSROOT)
            if(EXISTS "/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk")
                set(JAEGER_MACOS_SDK "/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk")
            else()
                execute_process(
                    COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE JAEGER_MACOS_SDK
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
            endif()
            if(JAEGER_MACOS_SDK)
                set(CMAKE_OSX_SYSROOT "${JAEGER_MACOS_SDK}" CACHE STRING "macOS SDK path" FORCE)
            endif()
        endif()
    endif()

    set(JAEGER_QT_ROOT "$ENV{JAEGER_QT_ROOT}")
    if(NOT JAEGER_QT_ROOT AND APPLE)
        foreach(JAEGER_QT_CANDIDATE
                "$ENV{HOME}/Qt/6.11.1/macos"
                "/Volumes/Jaeger/Qt/6.9.1/macos")
            if(EXISTS "${JAEGER_QT_CANDIDATE}")
                set(JAEGER_QT_ROOT "${JAEGER_QT_CANDIDATE}")
                break()
            endif()
        endforeach()
    endif()
    if(JAEGER_QT_ROOT)
        list(PREPEND CMAKE_PREFIX_PATH "${JAEGER_QT_ROOT}")
    endif()

    find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Sql)
endif()

set(DISKSERVER_SOURCES
    CMySql.cpp
    main.cpp
    server/tcpnet.cpp
    kernel/TCPKernel.cpp
    game/DefenseGameServer.cpp
    game/EarthWorld.cpp
)

set(DISKSERVER_HEADERS
    CMySql.h
    Packdef.h
    server/INet.h
    server/tcpnet.h
    kernel/IKernel.h
    kernel/TCPKernel.h
    game/DefenseGameServer.h
    game/EarthWorld.h
)

set(RVO2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RVO2/src")
add_library(NetDiskRVO2 STATIC
    ${RVO2_SOURCE_DIR}/Agent.cc
    ${RVO2_SOURCE_DIR}/KdTree.cc
    ${RVO2_SOURCE_DIR}/Line.cc
    ${RVO2_SOURCE_DIR}/Obstacle.cc
    ${RVO2_SOURCE_DIR}/RVOSimulator.cc
    ${RVO2_SOURCE_DIR}/Vector2.cc
)
target_include_directories(NetDiskRVO2 PUBLIC ${RVO2_SOURCE_DIR})
target_compile_definitions(NetDiskRVO2 PUBLIC RVO_STATIC_DEFINE)
set_target_properties(NetDiskRVO2 PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

qt_add_executable(DiskServerApp
    ${DISKSERVER_SOURCES}
    ${DISKSERVER_HEADERS}
)

target_include_directories(DiskServerApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/game
)

target_link_libraries(DiskServerApp PRIVATE
    NetDiskRVO2
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
)

set_target_properties(DiskServerApp PROPERTIES
    OUTPUT_NAME DiskServer
)

add_custom_command(TARGET DiskServerApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DiskServerApp>/tls"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tls/server.crt"
        "$<TARGET_FILE_DIR:DiskServerApp>/tls/server.crt"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DiskServerApp>/updates"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/updates/client_version.ini.example"
        "$<TARGET_FILE_DIR:DiskServerApp>/updates/client_version.ini.example"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/updates/client_version.ini.example"
        -DOUTPUT_FILE="$<TARGET_FILE_DIR:DiskServerApp>/updates/client_version.ini"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ensure_client_version_ini.cmake"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DiskServerApp>/earth"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/ne_110m_land.geojson"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/ne_110m_land.geojson"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/ne_110m_rivers_lake_centerlines.geojson"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/ne_110m_rivers_lake_centerlines.geojson"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/ne_110m_populated_places.geojson"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/ne_110m_populated_places.geojson"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/ne_110m_geography_regions_points.geojson"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/ne_110m_geography_regions_points.geojson"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/ne_110m_geography_regions_polys.geojson"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/ne_110m_geography_regions_polys.geojson"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/ne_110m_geography_marine_polys.geojson"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/ne_110m_geography_marine_polys.geojson"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/earth/README.md"
        "$<TARGET_FILE_DIR:DiskServerApp>/earth/README.md"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/diskserver.ini")
    add_custom_command(TARGET DiskServerApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/diskserver.ini"
            "$<TARGET_FILE_DIR:DiskServerApp>/diskserver.ini"
    )
else()
    add_custom_command(TARGET DiskServerApp POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT_FILE="$<TARGET_FILE_DIR:DiskServerApp>/diskserver.ini"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_default_diskserver_ini.cmake"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tls/server.key")
    add_custom_command(TARGET DiskServerApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tls/server.key"
            "$<TARGET_FILE_DIR:DiskServerApp>/tls/server.key"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(DefenseGamePathfindingTest
        tests/defense_game_pathfinding_test.cpp
        CMySql.cpp
        CMySql.h
        Packdef.h
        server/INet.h
        game/DefenseGameServer.cpp
        game/DefenseGameServer.h
    )
    target_include_directories(DefenseGamePathfindingTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/server
        ${CMAKE_CURRENT_SOURCE_DIR}/game
    )
    target_link_libraries(DefenseGamePathfindingTest PRIVATE
        NetDiskRVO2
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Sql
    )
    add_test(NAME defense_game_pathfinding COMMAND DefenseGamePathfindingTest)

    qt_add_executable(EarthWorldTest
        tests/earth_world_test.cpp
        CMySql.cpp
        CMySql.h
        Packdef.h
        game/EarthWorld.cpp
        game/EarthWorld.h
    )
    target_include_directories(EarthWorldTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/game
    )
    target_link_libraries(EarthWorldTest PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
    )
    target_compile_definitions(EarthWorldTest PRIVATE
        EARTH_TEST_DATA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/earth"
    )
    add_test(NAME earth_world COMMAND EarthWorldTest)
endif()
