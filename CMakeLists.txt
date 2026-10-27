cmake_minimum_required(VERSION 3.20)

# 版本号单一来源：VERSION 文件
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CRBOT_VERSION LIMIT_COUNT 1)
string(STRIP "${CRBOT_VERSION}" CRBOT_VERSION)

project(CRbot VERSION ${CRBOT_VERSION} LANGUAGES CXX)

if(APPLE)
    option(CRBOT_BUILD_MACOS_GUI "Build the native SwiftUI app (macOS 14+)" ON)
    if(CRBOT_BUILD_MACOS_GUI AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
    endif()
    if(CRBOT_BUILD_MACOS_GUI AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "14.0")
        message(FATAL_ERROR "The SwiftUI app requires macOS 14 or newer; use CRBOT_BUILD_MACOS_GUI=OFF for the CLI only")
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CRBOT_BUILD_NUMBER "AUTO" CACHE STRING "AUTO for local increments, or the CI workflow run number")
set(CRBOT_RELEASE_CHANNEL "prerelease" CACHE STRING "prerelease or stable")
set_property(CACHE CRBOT_RELEASE_CHANNEL PROPERTY STRINGS prerelease stable)
set(CRBOT_SOURCE_REVISION "unknown" CACHE STRING "Commit SHA recorded in the package")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set(CRBOT_METADATA_BYPRODUCTS
    "${CMAKE_CURRENT_BINARY_DIR}/generated/crbot_version.h"
    "${CMAKE_CURRENT_BINARY_DIR}/build-info.json")
if(APPLE AND CRBOT_BUILD_MACOS_GUI)
    list(APPEND CRBOT_METADATA_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/CRBot.app/Contents/Info.plist")
endif()
add_custom_target(crbot_build_metadata
    COMMAND ${CMAKE_COMMAND}
        "-DCRBOT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DCRBOT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DCRBOT_REQUESTED_BUILD_NUMBER=${CRBOT_BUILD_NUMBER}"
        "-DCRBOT_RELEASE_CHANNEL=${CRBOT_RELEASE_CHANNEL}"
        "-DCRBOT_SOURCE_REVISION=${CRBOT_SOURCE_REVISION}"
        "-DCRBOT_BUILD_CONFIG=$<CONFIG>"
        "-DCRBOT_MACOS_GUI=${CRBOT_BUILD_MACOS_GUI}"
        "-DCRBOT_MACOS_MIN_VERSION=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildMetadata.cmake"
    BYPRODUCTS ${CRBOT_METADATA_BYPRODUCTS}
    VERBATIM)

if(MSVC)
    add_compile_options("/utf-8")
    # Installed apps must run on PCs without Visual Studio / a separate VC CRT.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- 平台相关部分 ----
# Windows: Win32 进程/管道 + WIC 解码 + Win32 窗口 + 内嵌 adb.exe
# macOS:   POSIX 进程/管道 + ImageIO + SwiftUI / 终端面板 + 系统 adb
if(WIN32)
    set(CRBOT_PLATFORM_SRC
            src/platform_win.cpp
            src/proc_win.cpp
            src/png_io_wic.cpp)
    set(CRBOT_APP_SRC
            src/main.cpp
            src/gui_dashboard.cpp
            src/adb_resolve_win.cpp
            src/adb_resource.cpp)
    set(CRBOT_LINK_LIBS comctl32 windowscodecs ole32)
    add_compile_definitions(UNICODE _UNICODE)
elseif(APPLE)
    set(CRBOT_PLATFORM_SRC
            src/platform_posix.cpp
            src/proc_posix.cpp
            src/png_io_imageio.cpp)
    set(CRBOT_APP_SRC
            src/main.cpp
            src/tui_dashboard.cpp)
    # ImageIO 解 PNG，CoreGraphics 转 RGBA，CoreFoundation 传文件 / 内存数据
    set(CRBOT_LINK_LIBS
            "-framework ImageIO"
            "-framework CoreGraphics"
            "-framework CoreFoundation")
else()
    message(FATAL_ERROR "CRBot 目前只支持 Windows 和 macOS")
endif()

# 与平台无关的核心逻辑
set(CRBOT_CORE_SRC
        src/adb_client.cpp
        src/device.cpp
        src/image.cpp
        src/template_match.cpp
        src/ui_buttons.cpp
        src/elixir.cpp
        src/hand.cpp
        src/log_tee.cpp
        src/battler.cpp
        src/cr_game.cpp
        src/bot_runner.cpp
        src/navigator.cpp)

# 探针工具也要解 PNG，但不需要 adb / GUI
if(WIN32)
    set(CRBOT_PROBE_SRC src/png_io_wic.cpp src/platform_win.cpp)
    set(CRBOT_PROBE_LIBS windowscodecs ole32 shell32)
else()
    set(CRBOT_PROBE_SRC src/png_io_imageio.cpp src/platform_posix.cpp)
    set(CRBOT_PROBE_LIBS ${CRBOT_LINK_LIBS})
endif()

if(WIN32)
    # Preserve the existing Windows compilation/link layout. SwiftUI and its
    # shared static core are macOS-only and add no Windows dependencies.
    add_executable(CRbot WIN32 ${CRBOT_APP_SRC} ${CRBOT_CORE_SRC} ${CRBOT_PLATFORM_SRC})
    target_link_libraries(CRbot PRIVATE ${CRBOT_LINK_LIBS})
else()
    add_library(crbot_core STATIC ${CRBOT_CORE_SRC} ${CRBOT_PLATFORM_SRC})
    target_include_directories(crbot_core PUBLIC include)
    target_link_libraries(crbot_core PUBLIC ${CRBOT_LINK_LIBS})
    target_compile_definitions(crbot_core PRIVATE
            CRBOT_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/templates")
    add_executable(CRbot ${CRBOT_APP_SRC})
    target_link_libraries(CRbot PRIVATE crbot_core)
endif()

add_dependencies(CRbot crbot_build_metadata)
target_include_directories(CRbot PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(CRbot PRIVATE
        # 开发时找不到 exe 旁边的 assets 就退回源码树
        CRBOT_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/templates")

# 离线回归工具：对录像帧跑一遍圣水/手牌识别，用来验证算法
add_executable(elixir_probe
        src/probe_main.cpp
        src/image.cpp
        ${CRBOT_PROBE_SRC}
        src/elixir.cpp
        src/hand.cpp)
target_include_directories(elixir_probe PRIVATE include)
target_link_libraries(elixir_probe PRIVATE ${CRBOT_PROBE_LIBS})

# 离线验证按钮模板：ui_probe <screen.png> <template.png> [x y w h]
add_executable(ui_probe
        src/ui_probe.cpp
        src/image.cpp
        ${CRBOT_PROBE_SRC}
        src/template_match.cpp)
target_include_directories(ui_probe PRIVATE include)
target_link_libraries(ui_probe PRIVATE ${CRBOT_PROBE_LIBS})

if(APPLE)
    target_compile_options(crbot_core PRIVATE -Wall -Wextra)
    target_compile_options(CRbot PRIVATE -Wall -Wextra)
    target_compile_options(elixir_probe PRIVATE -Wall -Wextra)
    target_compile_options(ui_probe PRIVATE -Wall -Wextra)
endif()

if(APPLE AND CRBOT_BUILD_MACOS_GUI)
    add_library(crbot_macos_bridge STATIC macos/CRBotBridge.cpp)
    add_dependencies(crbot_macos_bridge crbot_build_metadata)
    target_include_directories(crbot_macos_bridge PUBLIC macos PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(crbot_macos_bridge PUBLIC crbot_core)
    target_compile_options(crbot_macos_bridge PRIVATE -Wall -Wextra)

    # swiftc is invoked explicitly so the default Unix Makefiles generator also
    # works. CMake's Swift language requires Ninja or Xcode.
    find_program(CRBOT_XCRUN xcrun REQUIRED)
    set(CRBOT_SWIFT_SOURCES
        macos/Models.swift macos/BotStore.swift macos/CRBotApp.swift
        macos/DashboardView.swift macos/DeviceDetailView.swift macos/LogConsoleView.swift)
    list(TRANSFORM CRBOT_SWIFT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    set(CRBOT_APP "${CMAKE_CURRENT_BINARY_DIR}/CRBot.app")
    set(CRBOT_SWIFT_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES CRBOT_ARCH_COUNT)
        if(NOT CRBOT_ARCH_COUNT EQUAL 1)
            message(FATAL_ERROR "Build the SwiftUI app for one architecture at a time (arm64 or x86_64)")
        endif()
        set(CRBOT_SWIFT_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    file(MAKE_DIRECTORY "${CRBOT_APP}/Contents")
    file(GLOB CRBOT_TEMPLATES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/templates/*.png")
    add_custom_command(OUTPUT "${CRBOT_APP}/Contents/MacOS/CRBot"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CRBOT_APP}/Contents/MacOS"
        COMMAND ${CRBOT_XCRUN} swiftc -parse-as-library -swift-version 6
            -target "${CRBOT_SWIFT_ARCH}-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}"
            -module-cache-path "${CMAKE_CURRENT_BINARY_DIR}/swift-module-cache"
            "$<IF:$<CONFIG:Debug>,-Onone,-O>" -g
            -import-objc-header "${CMAKE_CURRENT_SOURCE_DIR}/macos/CRBotBridge.h"
            ${CRBOT_SWIFT_SOURCES}
            $<TARGET_FILE:crbot_macos_bridge> $<TARGET_FILE:crbot_core>
            -lc++ -framework SwiftUI -framework AppKit
            -framework ImageIO -framework CoreGraphics -framework CoreFoundation
            -o "${CRBOT_APP}/Contents/MacOS/CRBot"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/templates" "${CRBOT_APP}/Contents/Resources/assets/templates"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${CRBOT_APP}/Contents/Resources/"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/build-info.json" "${CRBOT_APP}/Contents/Resources/"
        DEPENDS ${CRBOT_SWIFT_SOURCES} macos/CRBotBridge.h
            "${CRBOT_APP}/Contents/Info.plist" ${CRBOT_TEMPLATES}
            LICENSE THIRD_PARTY_NOTICES.md
            crbot_build_metadata crbot_macos_bridge crbot_core
        COMMENT "Building CRBot.app (SwiftUI, macOS 14+)"
        VERBATIM)
    add_custom_target(CRbot_macos ALL DEPENDS "${CRBOT_APP}/Contents/MacOS/CRBot")
endif()

# 模板随构建产物一起放到 exe 旁边，运行时按 exe 目录找
add_custom_command(TARGET CRbot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/templates
                $<TARGET_FILE_DIR:CRbot>/assets/templates
        COMMENT "copying button templates")
