# x64-mingw-static, but the ports are compiled with the same LLVM-MinGW clang (libc++)
# as the project. vcpkg's stock mingw triplet picks whatever x86_64-w64-mingw32-gcc is on
# PATH; with an MSYS2 GCC installed that silently produces libstdc++ binaries that do not
# link against a libc++ build (every gtest symbol "undefined").
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH PATH)

set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/llvm-mingw.cmake")
