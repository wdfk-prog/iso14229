# Yocto cross-compilation toolchain for client_demo (IMX8P/aarch64)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TOOLCHAIN_PREFIX "aarch64-poky-linux-" CACHE STRING "Yocto cross compiler prefix")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc"   CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++"   CACHE FILEPATH "C++ compiler")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc"   CACHE FILEPATH "ASM compiler")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_STRIP        "${TOOLCHAIN_PREFIX}strip"   CACHE FILEPATH "strip")

set(CMAKE_SYSROOT
    "/opt/fsl-imx-xwayland/6.1-mickledore/sysroots/armv8a-poky-linux"
    CACHE PATH
    "Yocto target sysroot")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Keep try-compile simple in cross environments.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
