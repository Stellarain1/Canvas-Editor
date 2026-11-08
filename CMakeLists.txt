cmake_minimum_required(VERSION 3.20)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target (Intel Macs on macOS 12.7.6 must still run it)")
endif()

project(CanvasEditor VERSION 1.3.0 LANGUAGES C CXX)
# what people see: "1.2.2b" (b = beta); the numeric version stays for Windows / macOS metadata
set(CANVAS_VERSION_SUFFIX "b")
set(CANVAS_VERSION_LABEL "${PROJECT_VERSION}${CANVAS_VERSION_SUFFIX}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CANVAS_BUILD_APP   "Build the Canvas Editor desktop app (needs OpenGL + GLFW)" ON)
option(CANVAS_BUILD_CLI   "Build canvas-cli (convert / check charts from a terminal)" ON)
option(CANVAS_BUILD_TESTS "Build the canvas_tests unit test runner" ON)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/MikuMikuWorld")
set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Depends")

# ---------------------------------------------------------------------------
# canvas_core: chart model, file formats and analysis. No OpenGL, no window,
# so the CLI and the tests can use exactly the same code the editor does.
# ---------------------------------------------------------------------------
set(CANVAS_CORE_SOURCES
    ${SRC_DIR}/BinaryReader.cpp
    ${SRC_DIR}/BinaryWriter.cpp
    ${SRC_DIR}/CustomScoreJsonSerializer.cpp
    ${SRC_DIR}/File.cpp
    ${SRC_DIR}/HistoryManager.cpp
    ${SRC_DIR}/IO.cpp
    ${SRC_DIR}/jsonIO.cpp
    ${SRC_DIR}/Language.cpp
    ${SRC_DIR}/Localization.cpp
    ${SRC_DIR}/Math.cpp
    ${SRC_DIR}/NativeScoreSerializer.cpp
    ${SRC_DIR}/Note.cpp
    ${SRC_DIR}/NotesFilter.cpp
    ${SRC_DIR}/Score.cpp
    ${SRC_DIR}/ScoreSerializer.cpp
    ${SRC_DIR}/ScoreStats.cpp
    ${SRC_DIR}/Sonolus.cpp
    ${SRC_DIR}/SonolusSerializer.cpp
    ${SRC_DIR}/SusExporter.cpp
    ${SRC_DIR}/SusParser.cpp
    ${SRC_DIR}/SusSerializer.cpp
    ${SRC_DIR}/Tempo.cpp
    ${SRC_DIR}/Utilities.cpp
    ${SRC_DIR}/ImGui/imgui.cpp
    ${SRC_DIR}/ImGui/imgui_draw.cpp
    ${SRC_DIR}/ImGui/imgui_tables.cpp
    ${SRC_DIR}/ImGui/imgui_widgets.cpp
    ${SRC_DIR}/ImGui/imgui_stdlib.cpp
)
file(GLOB_RECURSE CANVAS_CORE_NEW_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/Canvas/Core/*.cpp")
list(APPEND CANVAS_CORE_SOURCES ${CANVAS_CORE_NEW_SOURCES})
# audio decoding (miniaudio is compiled by Canvas/Core/AudioCodecs.cpp)
list(APPEND CANVAS_CORE_SOURCES "${DEPS_DIR}/stb_vorbis/stb_vorbis.c")

add_library(canvas_core STATIC ${CANVAS_CORE_SOURCES})

target_include_directories(canvas_core PUBLIC
    "${SRC_DIR}"
    "${DEPS_DIR}/DirectXMath-master/Inc"
    "${DEPS_DIR}/json"
    "${DEPS_DIR}/stb_image"
    "${DEPS_DIR}/miniaudio"
    "${DEPS_DIR}/stb_vorbis"
)

target_compile_definitions(canvas_core PUBLIC
    MIKUMIKUWORLD_VERSION="${PROJECT_VERSION}"
    CANVAS_VERSION="${CANVAS_VERSION_LABEL}"
)

if(MSVC)
    target_compile_definitions(canvas_core PUBLIC
        _CRT_SECURE_NO_WARNINGS
        _ENABLE_EXTENDED_ALIGNED_STORAGE
        NOMINMAX
        UNICODE
        _UNICODE
    )
    target_compile_options(canvas_core PUBLIC /utf-8)
    target_include_directories(canvas_core PUBLIC "${DEPS_DIR}/zlib/include")
    target_link_libraries(canvas_core PUBLIC "${DEPS_DIR}/zlib/lib/zlibstatic.lib")
else()
    target_compile_options(canvas_core PUBLIC
        -Wno-deprecated-declarations
        -Wno-unknown-pragmas
    )
    if(MINGW)
        # the same Windows setup the MSVC build gets; one self-contained .exe
        target_compile_definitions(canvas_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
        target_link_options(canvas_core PUBLIC -static -static-libgcc -static-libstdc++)
        set(ZLIB_USE_STATIC_LIBS ON)
    endif()
    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(canvas_core PUBLIC ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})
    if(NOT APPLE AND NOT WIN32)
        target_link_libraries(canvas_core PUBLIC m)
    endif()
endif()

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
    set_source_files_properties(
        "${SRC_DIR}/File.cpp"
        "${SRC_DIR}/IO.cpp"
        PROPERTIES LANGUAGE OBJCXX
    )
    target_link_libraries(canvas_core PUBLIC "-framework AppKit" "-framework Cocoa")
elseif(WIN32)
    target_link_libraries(canvas_core PUBLIC comdlg32 shell32 ole32 uuid)
endif()

# ---------------------------------------------------------------------------
# canvas-cli
# ---------------------------------------------------------------------------
if(CANVAS_BUILD_CLI)
    add_executable(canvas-cli "${CMAKE_CURRENT_SOURCE_DIR}/tools/cli/main.cpp")
    target_link_libraries(canvas-cli PRIVATE canvas_core)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(CANVAS_BUILD_TESTS)
    enable_testing()
    file(GLOB CANVAS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    add_executable(canvas_tests ${CANVAS_TEST_SOURCES})
    target_link_libraries(canvas_tests PRIVATE canvas_core)
    target_compile_definitions(canvas_tests PRIVATE
        CANVAS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
        CANVAS_SKILL_FEVER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/skill-fever-format"
    )
    add_test(NAME canvas_tests COMMAND canvas_tests)
endif()

# ---------------------------------------------------------------------------
# Canvas Editor desktop app
# ---------------------------------------------------------------------------
if(CANVAS_BUILD_APP)
    file(GLOB_RECURSE CANVAS_APP_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
    list(REMOVE_ITEM CANVAS_APP_SOURCES ${CANVAS_CORE_SOURCES})

    list(APPEND CANVAS_APP_SOURCES
        "${DEPS_DIR}/glad/src/glad.c"
    )

    add_executable(CanvasEditor ${CANVAS_APP_SOURCES})
    target_link_libraries(CanvasEditor PRIVATE canvas_core)

    target_include_directories(CanvasEditor PRIVATE
        "${DEPS_DIR}/glad/include"
        "${DEPS_DIR}/GLFW/include"
        "${DEPS_DIR}/miniaudio"
        "${DEPS_DIR}/stb_vorbis"
    )

    if(APPLE)
        set(MAC_APP_ICON "${SRC_DIR}/mmw_icon.icns")

        set_target_properties(CanvasEditor PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Canvas Editor"
            MACOSX_BUNDLE_ICON_FILE "mmw_icon.icns"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.stellarain1.canvaseditor"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${CANVAS_VERSION_LABEL}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        )

        target_sources(CanvasEditor PRIVATE ${MAC_APP_ICON})
        set_source_files_properties(${MAC_APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

        set_source_files_properties("${SRC_DIR}/main.cpp" PROPERTIES LANGUAGE OBJCXX)

        target_compile_definitions(CanvasEditor PRIVATE GL_SILENCE_DEPRECATION)

        find_package(OpenGL REQUIRED)
        find_package(glfw3 QUIET)
        if(NOT TARGET glfw)
            include(FetchContent)
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
            FetchContent_MakeAvailable(glfw)
        endif()

        target_link_libraries(CanvasEditor PRIVATE
            glfw
            OpenGL::GL
            "-framework IOKit"
            "-framework CoreVideo"
        )
    elseif(WIN32)
        enable_language(RC)
        target_sources(CanvasEditor PRIVATE "${SRC_DIR}/MikuMikuWorld.rc")
        if(MSVC)
            target_link_libraries(CanvasEditor PRIVATE "${DEPS_DIR}/GLFW/lib/glfw3.lib")
        else()
            # MinGW: the prebuilt glfw3.lib is for MSVC, so build GLFW from source
            include(FetchContent)
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
            FetchContent_MakeAvailable(glfw)
            target_link_libraries(CanvasEditor PRIVATE glfw)
        endif()
        target_link_libraries(CanvasEditor PRIVATE opengl32 version dwmapi mfplat mfreadwrite mfuuid ole32 d3d11)
        # GUI subsystem (no console window) while keeping the standard main() entry point
        set_target_properties(CanvasEditor PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            target_link_options(CanvasEditor PRIVATE "/ENTRY:mainCRTStartup")
        endif()
    else()
        if(CANVAS_GLFW_SOURCE_DIR)
            # cross builds (tools/build-cross.py): GLFW's X11 backend from source, with the X11 headers
            # only. GLFW and the app open libX11 / libGL with dlopen, so no GL or X11 library is linked.
            include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GlfwX11Manual.cmake")
            target_link_libraries(CanvasEditor PRIVATE glfw ${CMAKE_DL_LIBS})
            # no system GL headers here: glad declares everything the app uses
            target_compile_definitions(CanvasEditor PRIVATE GLFW_INCLUDE_NONE)
            target_include_directories(CanvasEditor PRIVATE "${CANVAS_X11_INCLUDE_DIR}")
        else()
            # Linux / other unix: system GLFW + OpenGL (see README for package names)
            find_package(OpenGL REQUIRED)
            find_package(glfw3 QUIET)
            if(NOT TARGET glfw)
                include(FetchContent)
                set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
                set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
                FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
                FetchContent_MakeAvailable(glfw)
            endif()

            target_link_libraries(CanvasEditor PRIVATE
                glfw
                OpenGL::GL
                ${CMAKE_DL_LIBS}
            )
        endif()
    endif()

    add_custom_command(TARGET CanvasEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/res"
            "$<TARGET_FILE_DIR:CanvasEditor>/res"
    )

    if(APPLE)
        add_custom_command(TARGET CanvasEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                "$<TARGET_FILE_DIR:CanvasEditor>/app_config.json"
                "$<TARGET_FILE_DIR:CanvasEditor>/imgui_config.ini"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:CanvasEditor>/auto_save"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:CanvasEditor>/library"
            VERBATIM
        )
        if(CMAKE_HOST_APPLE)
            add_custom_command(TARGET CanvasEditor POST_BUILD
                COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:CanvasEditor>"
                VERBATIM
            )
        endif()
    endif()

    install(TARGETS CanvasEditor RUNTIME DESTINATION . BUNDLE DESTINATION .)
    install(DIRECTORY "${SRC_DIR}/res" DESTINATION .)
endif()
