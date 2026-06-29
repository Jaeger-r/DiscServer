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

    find_package(Qt6 6.9.1 REQUIRED COMPONENTS Core Network Sql)
endif()

set(DISKSERVER_SOURCES
    CMySql.cpp
    main.cpp
    server/tcpnet.cpp
    kernel/TCPKernel.cpp
)

set(DISKSERVER_HEADERS
    CMySql.h
    Packdef.h
    server/INet.h
    server/tcpnet.h
    kernel/IKernel.h
    kernel/TCPKernel.h
)

qt_add_executable(DiskServerApp
    ${DISKSERVER_SOURCES}
    ${DISKSERVER_HEADERS}
)

target_include_directories(DiskServerApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel
)

target_link_libraries(DiskServerApp PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

set_target_properties(DiskServerApp PROPERTIES
    OUTPUT_NAME DiskServer
)

add_custom_command(TARGET DiskServerApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/diskserver.ini"
        "$<TARGET_FILE_DIR:DiskServerApp>/diskserver.ini"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:DiskServerApp>/tls"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tls/server.crt"
        "$<TARGET_FILE_DIR:DiskServerApp>/tls/server.crt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tls/server.key"
        "$<TARGET_FILE_DIR:DiskServerApp>/tls/server.key"
)
