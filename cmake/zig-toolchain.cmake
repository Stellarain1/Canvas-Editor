# Cross-compile with zig (https://ziglang.org): its bundled clang, libc++ and libc stubs mean the
# Linux and macOS builds can be made from Windows, Linux or macOS with nothing else installed.
#
#   cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake \
#         -DZIG_EXE=/path/to/zig -DZIG_TARGET=x86_64-linux-gnu.2.28 \
#         -DCANVAS_GLFW_SOURCE_DIR=/path/to/glfw-3.4 -DCANVAS_X11_INCLUDE_DIR=/path/to/x11/include
#
# ZIG_TARGET is any zig target: x86_64-linux-gnu.2.28 (glibc 2.28 and newer),
# aarch64-macos.12.0, x86_64-macos.12.0. For macOS also pass -DCANVAS_MACOS_SDK=<MacOSX sdk dir>.
# (tools/build-cross.py runs the whole thing and packages the zip.)
# CMake's compiler checks run this file again in a scratch project: pass the settings along
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ZIG_EXE ZIG_TARGET CANVAS_MACOS_SDK)

if(NOT ZIG_EXE OR NOT ZIG_TARGET)
    message(FATAL_ERROR "zig-toolchain.cmake needs -DZIG_EXE=<path to zig> and -DZIG_TARGET=<zig target>")
endif()

if(ZIG_TARGET MATCHES "macos")
    set(CMAKE_SYSTEM_NAME Darwin)
    set(CMAKE_OSX_SYSROOT "${CANVAS_MACOS_SDK}" CACHE PATH "" FORCE)
elseif(ZIG_TARGET MATCHES "linux")
    set(CMAKE_SYSTEM_NAME Linux)
else()
    message(FATAL_ERROR "zig-toolchain.cmake: unsupported target ${ZIG_TARGET}")
endif()
string(REGEX MATCH "^[^-]+" _arch "${ZIG_TARGET}")
set(CMAKE_SYSTEM_PROCESSOR ${_arch})

# CMake wants one program per tool: tiny launchers that put the zig sub-command and target in front
set(_launchers "${CMAKE_BINARY_DIR}/zig-launchers")
file(MAKE_DIRECTORY "${_launchers}")
set(_sysroot_args "")
if(ZIG_TARGET MATCHES "macos" AND CANVAS_MACOS_SDK)
    # zig brings its own libc headers: the SDK's /usr/include only fills in what zig lacks (libDER for
    # Security.framework). Apple's older framework headers trip clang's newer enum-base check.
    set(_sysroot_args "-isysroot ${CANVAS_MACOS_SDK} -idirafter ${CANVAS_MACOS_SDK}/usr/include -F${CANVAS_MACOS_SDK}/System/Library/Frameworks -L${CANVAS_MACOS_SDK}/usr/lib -Wno-elaborated-enum-base -Wno-error=elaborated-enum-base")
    # miniaudio uses the macOS 12 name of this constant; SDKs before 12 call it ...Master
    if(CANVAS_MACOS_SDK MATCHES "MacOSX(10|11)\.")
        set(_sysroot_args "${_sysroot_args} -DkAudioObjectPropertyElementMain=kAudioObjectPropertyElementMaster")
    endif()
endif()
foreach(_tool cc c++ ar ranlib)
    if(CMAKE_HOST_WIN32)
        set(_file "${_launchers}/zig-${_tool}.cmd")
        file(TO_NATIVE_PATH "${ZIG_EXE}" _zig_native)
        if(_tool STREQUAL "cc" OR _tool STREQUAL "c++")
            file(WRITE "${_file}" "@\"${_zig_native}\" ${_tool} -target ${ZIG_TARGET} ${_sysroot_args} %*\r\n")
        else()
            file(WRITE "${_file}" "@\"${_zig_native}\" ${_tool} %*\r\n")
        endif()
    else()
        set(_file "${_launchers}/zig-${_tool}")
        if(_tool STREQUAL "cc" OR _tool STREQUAL "c++")
            file(WRITE "${_file}" "#!/bin/sh\nexec \"${ZIG_EXE}\" ${_tool} -target ${ZIG_TARGET} ${_sysroot_args} \"$@\"\n")
        else()
            file(WRITE "${_file}" "#!/bin/sh\nexec \"${ZIG_EXE}\" ${_tool} \"$@\"\n")
        endif()
        execute_process(COMMAND chmod +x "${_file}")
    endif()
endforeach()
if(CMAKE_HOST_WIN32)
    set(_ext ".cmd")
else()
    set(_ext "")
endif()
set(CMAKE_C_COMPILER "${_launchers}/zig-cc${_ext}")
set(CMAKE_CXX_COMPILER "${_launchers}/zig-c++${_ext}")
# macOS: Objective-C / C++ files (GLFW's Cocoa backend, the dock icon) are clang's too
set(CMAKE_OBJC_COMPILER "${_launchers}/zig-cc${_ext}")
set(CMAKE_OBJCXX_COMPILER "${_launchers}/zig-c++${_ext}")
# CMake wants Apple's binutils to exist for Darwin targets; they only matter for install-time
# fix-ups of shared libraries, which a static build never does
if(CMAKE_HOST_WIN32)
    file(WRITE "${_launchers}/noop.cmd" "@exit /b 0\r\n")
    set(_noop "${_launchers}/noop.cmd")
else()
    file(WRITE "${_launchers}/noop" "#!/bin/sh\nexit 0\n")
    execute_process(COMMAND chmod +x "${_launchers}/noop")
    set(_noop "${_launchers}/noop")
endif()
if(ZIG_TARGET MATCHES "macos")
    foreach(_var CMAKE_INSTALL_NAME_TOOL CMAKE_OTOOL CMAKE_LIPO)
        set(${_var} "${_noop}" CACHE FILEPATH "" FORCE)
    endforeach()
endif()
set(CMAKE_AR "${_launchers}/zig-ar${_ext}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_launchers}/zig-ranlib${_ext}" CACHE FILEPATH "" FORCE)

# zig's lld can drop what nothing uses, and static libc++ is in the binary: nothing to install
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
