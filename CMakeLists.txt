cmake_minimum_required(VERSION 3.21)

# OpenCV 来自 vcpkg，若未显式指定工具链则自动使用 E:/Lib/vcpkg
set(VCPKG_ROOT_DIR "E:/Lib/vcpkg" CACHE PATH "vcpkg root directory")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain file")
endif()

project(ScreenCutOcrCn VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# onnxruntime 解压目录，需包含 include/ 与 lib/
set(ONNXRUNTIME_ROOT "E:/Lib/onnxruntime-win-x64-1.31.1" CACHE PATH "ONNX Runtime root directory")
foreach(required_file
        "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "onnxruntime 文件缺失: ${required_file}\n请用 -DONNXRUNTIME_ROOT=<解压目录> 指定正确路径")
    endif()
endforeach()

# Qt6 安装目录（含 lib/cmake/Qt6）
set(QT_ROOT "D:/Lib/Qt/6.6.2/msvc2019_64" CACHE PATH "Qt6 root directory")
if(EXISTS "${QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()

find_package(OpenCV CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Widgets Gui Core REQUIRED)

add_executable(ScreenCutOcrCn
    main.cpp
    main_window.h main_window.cpp
    screen_capture.h screen_capture.cpp
    cnocr.h cnocr.cpp
    cnstd.h cnstd.cpp
    modle.h modle.cpp
    det_box.h
    db_postprocess.h db_postprocess.cpp
    text_utils.h text_utils.cpp
    autogui.h autogui.cpp
    hotkey.hpp
)
target_compile_options(ScreenCutOcrCn PRIVATE "/utf-8" "/Zc:__cplusplus" "/permissive-")
target_compile_definitions(ScreenCutOcrCn PRIVATE NOMINMAX)
target_include_directories(ScreenCutOcrCn PRIVATE
    ${PROJECT_SOURCE_DIR}
    "${ONNXRUNTIME_ROOT}/include"
)
target_link_libraries(ScreenCutOcrCn PRIVATE
    ${OpenCV_LIBS}
    "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    Qt6::Widgets Qt6::Gui Qt6::Core
)

# Release 使用 Windows 子系统，启动时不弹出控制台窗口；Debug 保留控制台便于查看日志
if(MSVC)
    target_link_options(ScreenCutOcrCn PRIVATE
        "$<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>"
        "$<$<CONFIG:Release>:/ENTRY:mainCRTStartup>"
    )
endif()

# 运行时依赖：onnxruntime 的 DLL 拷到输出目录；OpenCV 的 DLL 由 vcpkg applocal 自动部署
add_custom_command(TARGET ScreenCutOcrCn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
        "$<TARGET_FILE_DIR:ScreenCutOcrCn>"
    VERBATIM
)

# 程序按相对路径读取 modle/*.onnx 与 dict/*.txt，拷到可执行文件旁
file(GLOB CNOCR_MODEL_FILES "${PROJECT_SOURCE_DIR}/modle/*.onnx")
file(GLOB CNOCR_DICT_FILES "${PROJECT_SOURCE_DIR}/dict/*.txt")
add_custom_command(TARGET ScreenCutOcrCn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ScreenCutOcrCn>/modle"
        "$<TARGET_FILE_DIR:ScreenCutOcrCn>/dict"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CNOCR_MODEL_FILES} "$<TARGET_FILE_DIR:ScreenCutOcrCn>/modle"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CNOCR_DICT_FILES} "$<TARGET_FILE_DIR:ScreenCutOcrCn>/dict"
    VERBATIM
)

# Qt 平台插件：优先用 windeployqt 部署，缺失时从 Qt 插件目标反推 plugins 目录复制
set(QT_BIN_DIR "")
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
if(QT_QMAKE_EXECUTABLE)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
endif()
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${QT_BIN_DIR})

# vcpkg 的 qtbase 不安装 windeployqt，通过平台插件目标定位 plugins 目录
set(QT_PLUGINS_DIR "")
if(NOT WINDEPLOYQT_EXECUTABLE AND TARGET Qt6::QWindowsIntegrationPlugin)
    get_target_property(QT_PLUGIN_FILE Qt6::QWindowsIntegrationPlugin IMPORTED_LOCATION_RELEASE)
    if(NOT QT_PLUGIN_FILE)
        get_target_property(QT_PLUGIN_FILE Qt6::QWindowsIntegrationPlugin IMPORTED_LOCATION)
    endif()
    if(QT_PLUGIN_FILE)
        get_filename_component(_qt_platforms_dir "${QT_PLUGIN_FILE}" DIRECTORY)
        get_filename_component(QT_PLUGINS_DIR "${_qt_platforms_dir}" DIRECTORY)
    endif()
endif()
if(NOT QT_PLUGINS_DIR AND DEFINED QT6_INSTALL_PLUGINS AND EXISTS "${QT6_INSTALL_PLUGINS}/platforms")
    set(QT_PLUGINS_DIR "${QT6_INSTALL_PLUGINS}")
endif()

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ScreenCutOcrCn POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-compiler-runtime
                "$<TARGET_FILE:ScreenCutOcrCn>"
        VERBATIM
    )
elseif(QT_PLUGINS_DIR)
    add_custom_command(TARGET ScreenCutOcrCn POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_PLUGINS_DIR}" "$<TARGET_FILE_DIR:ScreenCutOcrCn>/plugins"
        VERBATIM
    )
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" "[Paths]\nPlugins=plugins\n")
    add_custom_command(TARGET ScreenCutOcrCn POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" "$<TARGET_FILE_DIR:ScreenCutOcrCn>"
        VERBATIM
    )
else()
    message(WARNING "未找到 windeployqt 或 Qt 插件目录，运行前需手动部署 platforms/qwindows.dll")
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
