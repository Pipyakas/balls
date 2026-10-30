# Cross-compiles balls_core and the physics plugins for Linux x86_64 with the toolchain Unreal Engine itself uses
# (LINUX_MULTIARCH_ROOT, installed from Epic's v26_clang-20.1.8-rockylinux8). It must be that compiler and that
# libc++: the plugins pass std::string and std::span across the plugin boundary, so core, plugins and the game
# module have to share one standard library.
if(NOT DEFINED ENV{LINUX_MULTIARCH_ROOT})
    message(FATAL_ERROR "LINUX_MULTIARCH_ROOT is not set; install Unreal's Linux cross-compile toolchain.")
endif()
file(TO_CMAKE_PATH "$ENV{LINUX_MULTIARCH_ROOT}" BALLS_LINUX_TOOLCHAIN)
set(BALLS_LINUX_SYSROOT "${BALLS_LINUX_TOOLCHAIN}/x86_64-unknown-linux-gnu")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "${BALLS_LINUX_SYSROOT}/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${BALLS_LINUX_SYSROOT}/bin/clang++.exe")
set(CMAKE_AR "${BALLS_LINUX_SYSROOT}/bin/llvm-ar.exe")
set(CMAKE_RANLIB "${BALLS_LINUX_SYSROOT}/bin/llvm-ranlib.exe")
set(CMAKE_SYSROOT "${BALLS_LINUX_SYSROOT}")
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-linux-gnu)

# Unreal's flags: its own libc++ headers and no system C++ headers.
set(BALLS_LINUX_CXX_INCLUDES "-nostdinc++ -isystem \"${BALLS_LINUX_SYSROOT}/include\" -isystem \"${BALLS_LINUX_SYSROOT}/include/c++/v1\"")
set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC ${BALLS_LINUX_CXX_INCLUDES} -fbinutils-version=2.36")

# lld, and libc++ plus its ABI library linked statically, exactly as UnrealBuildTool links the game.
set(BALLS_LINUX_LIBCXX "\"${BALLS_LINUX_SYSROOT}/lib64/libc++.a\" \"${BALLS_LINUX_SYSROOT}/lib64/libc++abi.a\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld.exe -nodefaultlibs")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld.exe -nodefaultlibs")
set(CMAKE_CXX_STANDARD_LIBRARIES "${BALLS_LINUX_LIBCXX} -lm -lc -lpthread -ldl -lgcc_s")
set(CMAKE_C_STANDARD_LIBRARIES "-lm -lc -lpthread -ldl")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH "${BALLS_LINUX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
