# MobileGL - mg-air/CMakeLists.txt
# mg-3backends: wrapper module building the vendored MobileGlues core as
# libmg_gles.so (Air 6.0 "GLES" backend, GL -> OpenGL ES with FSR1).
#
# This module keeps a SEPARATE CMake invocation from the MobileGL core on
# purpose: both trees vendor their own glslang/SPIRV-Cross with different
# options, and CMake target names would collide in a single invocation.

cmake_minimum_required(VERSION 3.22.1)
# NOTE: the vendored MobileGlues-cpp/CMakeLists.txt names its target with
# ${CMAKE_PROJECT_NAME}, which always resolves to the TOP-LEVEL project name.
# Keep this project() call named "mobileglues" so the subtree builds its
# canonical `mobileglues` target (OUTPUT_NAME mg_gles -> libmg_gles.so).
project(mobileglues C CXX)

# The submodule mounts the MobileGlues REPO ROOT (which carries its own CI,
# gradle wrapper and .gitmodules); the C++ tree lives one level deeper, in the
# repo's own MobileGlues-cpp/ directory.
set(MG_GLES_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../MobileGlues-cpp/MobileGlues-cpp)
add_subdirectory(${MG_GLES_SRC} mg_gles_build)
