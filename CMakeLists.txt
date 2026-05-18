cmake_minimum_required(VERSION 3.16)
project(camera2_wayland CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WL   REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL  REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

# libhybris-common has no .pc file; use it directly
find_library(HYBRIS_COMMON_LIB hybris-common REQUIRED)
set(HYBRIS_INCLUDE_DIRS /usr/include)

add_executable(camera2_wayland
    src/ndk_loader.cpp
    src/image_reader.cpp
    src/camera_session.cpp
    src/main.cpp
)

target_include_directories(camera2_wayland PRIVATE
    src/
    ${CMAKE_SOURCE_DIR}/android-headers
    ${HYBRIS_INCLUDE_DIRS}
    ${WL_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
)

target_link_libraries(camera2_wayland
    ${HYBRIS_COMMON_LIB}
    ${WL_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
)

target_compile_options(camera2_wayland PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    # Inject Android API compat macros before all TUs
    -include ${CMAKE_SOURCE_DIR}/android-headers/android_compat.h
)
