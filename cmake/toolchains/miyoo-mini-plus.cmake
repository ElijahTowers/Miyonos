set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED ENV{MIYOO_TOOLCHAIN_ROOT})
  message(FATAL_ERROR "MIYOO_TOOLCHAIN_ROOT must point to the extracted mini toolchain")
endif()

set(MIYOO_TOOLCHAIN_BIN "$ENV{MIYOO_TOOLCHAIN_ROOT}/prebuilt/bin")
set(MIYOO_SYSROOT
    "$ENV{MIYOO_TOOLCHAIN_ROOT}/prebuilt/arm-linux-gnueabihf/libc")
if(NOT EXISTS "${MIYOO_TOOLCHAIN_BIN}/arm-linux-gnueabihf-g++")
  message(FATAL_ERROR
          "MIYOO_TOOLCHAIN_ROOT does not contain the mini_toolchain-v1.0 layout")
endif()

set(CMAKE_C_COMPILER
    "${MIYOO_TOOLCHAIN_BIN}/arm-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER
    "${MIYOO_TOOLCHAIN_BIN}/arm-linux-gnueabihf-g++")
set(CMAKE_SYSROOT "${MIYOO_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${MIYOO_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
