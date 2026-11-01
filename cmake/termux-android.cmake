# Cross-compile Android app libraries with the ARM64 compiler running on the phone.
# Use NDK headers and libraries, so the APK never depends on Termux's private libraries.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(ANDROID TRUE)
set(ANDROID_ABI arm64-v8a)
set(CMAKE_SKIP_RPATH TRUE CACHE BOOL "No build-machine paths in the APK" FORCE)

set(GALLERY_NDK "$ENV{GALLERY_NDK}")
set(GALLERY_PREFIX "$ENV{PREFIX}")
set(GALLERY_SYSROOT "${GALLERY_NDK}/toolchains/llvm/prebuilt/linux-x86_64/sysroot")
if(NOT EXISTS "${GALLERY_SYSROOT}/usr/include/jni.h")
  message(FATAL_ERROR "Set GALLERY_NDK to the installed Android NDK directory")
endif()
set(CMAKE_SYSROOT "${GALLERY_SYSROOT}")
list(APPEND CMAKE_PREFIX_PATH "${GALLERY_PREFIX}")
# NDK r27 has Vulkan 1.3 C headers but no Vulkan-Hpp; Termux ships both (1.4)
# plus SPIRV-Headers. Stage only those directories: adding $PREFIX/include
# wholesale would put Termux's glibc headers ahead of Bionic's. ggml-vulkan
# dispatches Vulkan calls through vkGetInstanceProcAddr, so the NDK stub
# remains the link-time library.
set(GALLERY_VULKAN_STAGE "${CMAKE_BINARY_DIR}/termux-vulkan-headers")
if(NOT EXISTS "${GALLERY_VULKAN_STAGE}/include/vulkan/vulkan.hpp")
  file(COPY "${GALLERY_PREFIX}/include/vulkan" "${GALLERY_PREFIX}/include/vk_video"
       "${GALLERY_PREFIX}/include/spirv" DESTINATION "${GALLERY_VULKAN_STAGE}/include")
endif()
file(WRITE "${GALLERY_VULKAN_STAGE}/SPIRV-HeadersConfig.cmake"
"add_library(SPIRV-Headers::SPIRV-Headers INTERFACE IMPORTED)
set_target_properties(SPIRV-Headers::SPIRV-Headers PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES \"${GALLERY_VULKAN_STAGE}/include\")
")
set(Vulkan_INCLUDE_DIR "${GALLERY_VULKAN_STAGE}/include" CACHE PATH "Vulkan headers" FORCE)
set(Vulkan_LIBRARY "${GALLERY_SYSROOT}/usr/lib/aarch64-linux-android/31/libvulkan.so" CACHE FILEPATH "Android Vulkan loader" FORCE)
set(Vulkan_GLSLC_EXECUTABLE "${GALLERY_PREFIX}/bin/glslc" CACHE FILEPATH "Host glslc" FORCE)
set(SPIRV-Headers_DIR "${GALLERY_VULKAN_STAGE}" CACHE PATH "SPIRV-Headers config" FORCE)
set(CMAKE_C_COMPILER "${GALLERY_PREFIX}/bin/clang")
set(CMAKE_CXX_COMPILER "${GALLERY_PREFIX}/bin/clang++")
set(CMAKE_C_COMPILER_TARGET aarch64-linux-android31)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-android31)
set(CMAKE_AR "${GALLERY_PREFIX}/bin/llvm-ar")
set(CMAKE_RANLIB "${GALLERY_PREFIX}/bin/llvm-ranlib")
set(CMAKE_STRIP "${GALLERY_PREFIX}/bin/llvm-strip")
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++ -nostdinc++ -isystem ${GALLERY_SYSROOT}/usr/include/c++/v1")
set(GALLERY_UNWIND "${GALLERY_NDK}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/18/lib/linux/aarch64")
# Search the API-specific shared stubs before the generic static archives.
# Otherwise -lc/-lm/-ldl embed Bionic in the plugin, whose initial-exec TLS
# Android's linker rejects when System.loadLibrary loads it into the app.
set(GALLERY_LINK_FLAGS "-fno-termux-rpath -nostdlib++ -L${GALLERY_SYSROOT}/usr/lib/aarch64-linux-android/31 -L${GALLERY_SYSROOT}/usr/lib/aarch64-linux-android -L${GALLERY_UNWIND} -Wl,--no-as-needed -lc++_shared -Wl,--as-needed -Wl,--no-undefined -Wl,-z,max-page-size=16384")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${GALLERY_LINK_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${GALLERY_LINK_FLAGS}")
# Refresh the flags in existing local build trees after toolchain fixes too.
set(CMAKE_SHARED_LINKER_FLAGS "${GALLERY_LINK_FLAGS}" CACHE STRING "Android shared linker flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "${GALLERY_LINK_FLAGS}" CACHE STRING "Android executable linker flags" FORCE)
