# GLFW 3.4's X11 backend, compiled straight from its sources. GLFW's own CMake insists on finding
# the X11 *libraries* with find_package(X11), which a cross build does not have; the backend only
# needs the X11 *headers* (it loads libX11, libXrandr, ... with dlopen at start-up), so this is all
# it takes. Wayland desktops run it through XWayland.
#
# Needs CANVAS_GLFW_SOURCE_DIR (a GLFW 3.4 checkout) and CANVAS_X11_INCLUDE_DIR (X11 headers;
# tools/fetch-x11-headers.py downloads them).
set(_g "${CANVAS_GLFW_SOURCE_DIR}/src")
add_library(glfw STATIC
    ${_g}/context.c ${_g}/init.c ${_g}/input.c ${_g}/monitor.c ${_g}/platform.c ${_g}/vulkan.c ${_g}/window.c
    ${_g}/egl_context.c ${_g}/osmesa_context.c
    ${_g}/null_init.c ${_g}/null_monitor.c ${_g}/null_window.c ${_g}/null_joystick.c
    ${_g}/posix_module.c ${_g}/posix_thread.c ${_g}/posix_time.c ${_g}/posix_poll.c ${_g}/linux_joystick.c
    ${_g}/x11_init.c ${_g}/x11_monitor.c ${_g}/x11_window.c ${_g}/xkb_unicode.c ${_g}/glx_context.c
)
target_compile_definitions(glfw PRIVATE _GLFW_X11 _DEFAULT_SOURCE)
target_include_directories(glfw
    PUBLIC "${CANVAS_GLFW_SOURCE_DIR}/include"
    PRIVATE "${_g}" "${CANVAS_X11_INCLUDE_DIR}")
target_link_libraries(glfw PRIVATE ${CMAKE_DL_LIBS} m Threads::Threads)
