cmake_minimum_required(VERSION 3.20)
project(StereoVideoPlayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 第三方库根目录
set(THIRD_PARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")

# FFmpeg：Windows 使用预编译库，Linux 使用 pkg-config
if(WIN32)
    set(FFMPEG_DIR "${THIRD_PARTY_ROOT}/FFmpeg")
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_DIR}/include")
    set(FFMPEG_LIBS
        "${FFMPEG_DIR}/lib/avformat.lib"
        "${FFMPEG_DIR}/lib/avcodec.lib"
        "${FFMPEG_DIR}/lib/avutil.lib"
        "${FFMPEG_DIR}/lib/swscale.lib"
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavformat>=58
        libavcodec>=58
        libavutil>=56
        libswscale>=5
    )
    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
    set(FFMPEG_LIBS ${FFMPEG_LIBRARIES})
endif()

if(WIN32)
    # Windows：使用 Unicode 与 UTF-8 编译选项
    add_definitions(-DUNICODE -D_UNICODE)
    add_compile_options(/sdl-)
    add_compile_options(/utf-8)

    # GLEW
    set(GLEW_DIR "${THIRD_PARTY_ROOT}/GLEW")
    set(GLEW_INCLUDE_DIRS "${GLEW_DIR}/include")
    set(GLEW_LIBS "${GLEW_DIR}/lib/glew32.lib")

    add_executable(${PROJECT_NAME} WIN32
        src/main.cpp
        src/FFmpegVideoReader.cpp
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${GLEW_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
        src
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${GLEW_LIBS}
        ${FFMPEG_LIBS}
        opengl32.lib
        glu32.lib
        gdi32.lib
        user32.lib
        kernel32.lib
    )

    # 复制运行所需的 DLL 到输出目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GLEW_DIR}/dll/glew32.dll"
            "${FFMPEG_DIR}/dll/avcodec-58.dll"
            "${FFMPEG_DIR}/dll/avformat-58.dll"
            "${FFMPEG_DIR}/dll/avutil-56.dll"
            "${FFMPEG_DIR}/dll/swscale-5.dll"
            "${FFMPEG_DIR}/dll/swresample-3.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
else()
    # Linux：X11 + GLX + GLEW + OpenGL + FFmpeg
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)

    add_executable(${PROJECT_NAME}
        src/main_linux.cpp
        src/FFmpegVideoReader.cpp
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
        src
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${FFMPEG_LIBS}
        GLEW::GLEW
        OpenGL::GL
        X11
        pthread
        stdc++fs
    )
endif()

# ================= 安装与打包（CPack，仅 Linux） =================
# 为 `cpack` 提供基础打包（TGZ），包含可执行文件与便携启动脚本。
# 注意：该 TGZ 不内置系统依赖库，目标机需自行安装 FFmpeg/GLEW/OpenGL/X11 开发库；
#       若需自带依赖的便携包，请使用 deploy/package_linux.sh。
if(NOT WIN32)
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
    install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/deploy/run.sh" DESTINATION .)

    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VERSION "1.0.0")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OpenGL Quad Buffer 立体视频播放器")
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SOURCE_GENERATOR "TGZ")
    set(CPACK_SOURCE_IGNORE_FILES "/build/;/\\.git/;/.workbuddy/")
    include(CPack)
endif()
