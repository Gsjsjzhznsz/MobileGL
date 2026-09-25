# MobileGL - dispatcher/CMakeLists.txt
# mg-3backends: builds the unified renderer entry `mobileglues`
# (libmobileglues.so) — a thin lazy forwarder over the selected backend core.
#
# Inputs come from the parent MobileGL build (repo root CMakeLists):
#   MOBILEGL_INCLUDE_DIR : directory containing GL/ and EGL/ Khronos headers

# Generate the forwarder table from the vendored Khronos headers at configure
# time. The generator hard-fails on varargs prototypes so silent gaps in the
# dlsym surface are impossible.
set(MG_FWD_C ${CMAKE_CURRENT_BINARY_DIR}/forwarders.c)
add_custom_command(
    OUTPUT ${MG_FWD_C}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_forwarders.py
            --gl-header ${MOBILEGL_INCLUDE_DIR}/GL/glcorearb.h
            --gl-header ${MOBILEGL_INCLUDE_DIR}/GL/glext.h
            --egl-header ${MOBILEGL_INCLUDE_DIR}/EGL/egl.h
            --egl-header ${MOBILEGL_INCLUDE_DIR}/EGL/eglext.h
            --out ${MG_FWD_C}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_forwarders.py
            ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher.h
            ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher.c
            ${MOBILEGL_INCLUDE_DIR}/GL/glcorearb.h
            ${MOBILEGL_INCLUDE_DIR}/GL/glext.h
            ${MOBILEGL_INCLUDE_DIR}/EGL/egl.h
            ${MOBILEGL_INCLUDE_DIR}/EGL/eglext.h
    COMMENT "mg-dispatch: generating GL/EGL forwarders"
    VERBATIM
)

add_library(mobileglues SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher.c
    ${MG_FWD_C}
)

target_include_directories(mobileglues PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mobileglues PRIVATE -O2 -fvisibility=hidden -Wall)
# Only eglGetProcAddress + the generated forwarders carry default visibility;
# everything else stays hidden so the launcher's dlsym surface is exactly the
# GL/EGL contract.
if(ANDROID)
    target_link_libraries(mobileglues PRIVATE log dl)
else()
    target_link_libraries(mobileglues PRIVATE ${CMAKE_DL_LIBS})
endif()
set_target_properties(mobileglues PROPERTIES
    OUTPUT_NAME "mobileglues"
    C_STANDARD 11
)
