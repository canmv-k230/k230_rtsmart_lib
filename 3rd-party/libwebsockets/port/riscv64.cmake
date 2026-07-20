# RT-Smart RISC-V cross-compile toolchain for cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# The RT-Smart Make build passes an absolute prefix here.  This keeps CMake
# independent of PATH, while retaining a useful standalone-CMake fallback.
if(NOT DEFINED CROSS_COMPILE OR "${CROSS_COMPILE}" STREQUAL "")
  if(DEFINED ENV{CROSS_COMPILE} AND NOT "$ENV{CROSS_COMPILE}" STREQUAL "")
    set(CROSS_COMPILE "$ENV{CROSS_COMPILE}" CACHE STRING "Cross-toolchain executable prefix" FORCE)
  else()
    set(CROSS_COMPILE "riscv64-unknown-linux-musl-" CACHE STRING "Cross-toolchain executable prefix" FORCE)
  endif()
endif()

# Do not override explicitly supplied CMake compiler paths.
if(NOT DEFINED CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER "${CROSS_COMPILE}gcc")
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}g++")
endif()
if(NOT DEFINED CMAKE_AR)
  set(CMAKE_AR "${CROSS_COMPILE}ar")
endif()
if(NOT DEFINED CMAKE_RANLIB)
  set(CMAKE_RANLIB "${CROSS_COMPILE}ranlib")
endif()
if(NOT DEFINED CMAKE_STRIP)
  set(CMAKE_STRIP "${CROSS_COMPILE}strip")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -Wl,--gc-sections")
