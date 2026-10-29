# Builds the core for a Linux host with the compiler and the standard library Unreal uses on that host: the engine's
# in-tree clang and its bundled libc++. The core passes std::string and std::span to the game module, so both must
# agree on the standard library; a native build with the system libstdc++ would break that.
#
# LinuxToolchain.cmake does the same for a Windows host cross-compiling to Linux (clang.exe). This one is the native
# twin, selected by BuildCoreLinux.sh, which exports BALLS_LINUX_TOOLCHAIN: an environment variable, because CMake
# reads the toolchain file again inside every try_compile, where command-line variables are not passed on.
if(NOT DEFINED ENV{BALLS_LINUX_TOOLCHAIN})
    message(FATAL_ERROR "BALLS_LINUX_TOOLCHAIN is not set; point it at the engine's x86_64-unknown-linux-gnu toolchain.")
endif()
file(TO_CMAKE_PATH "$ENV{BALLS_LINUX_TOOLCHAIN}" BALLS_LINUX_SYSROOT)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "${BALLS_LINUX_SYSROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${BALLS_LINUX_SYSROOT}/bin/clang++")
set(CMAKE_AR "${BALLS_LINUX_SYSROOT}/bin/llvm-ar")
set(CMAKE_RANLIB "${BALLS_LINUX_SYSROOT}/bin/llvm-ranlib")
set(CMAKE_SYSROOT "${BALLS_LINUX_SYSROOT}")
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-linux-gnu)

set(BALLS_LINUX_CXX_INCLUDES "-nostdinc++ -isystem \"${BALLS_LINUX_SYSROOT}/include\" -isystem \"${BALLS_LINUX_SYSROOT}/include/c++/v1\"")
set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC ${BALLS_LINUX_CXX_INCLUDES} -fbinutils-version=2.36")

# lld, and libc++ plus its ABI library linked statically, as UnrealBuildTool links the game. Unlike the cross twin
# this keeps the default libraries: the in-tree sysroot's libc is a cross-compile stub, and only the host's own
# crt and libc can start an executable or a shared library here.
set(BALLS_LINUX_LIBCXX "\"${BALLS_LINUX_SYSROOT}/lib64/libc++.a\" \"${BALLS_LINUX_SYSROOT}/lib64/libc++abi.a\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_CXX_STANDARD_LIBRARIES "${BALLS_LINUX_LIBCXX}")
