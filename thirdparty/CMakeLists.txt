#
# ---------------------------------------------------------
# EnTT (entity-component framework)
# ---------------------------------------------------------
#
include(FetchContent)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/entt/CMakeLists.txt")
    add_subdirectory(entt)
else()
    set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ENTT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENTT_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(ENTT_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        EnTT
        GIT_REPOSITORY "https://github.com/skypjack/entt.git"
        GIT_TAG "v3.13.2"
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(EnTT)
endif()

if(TARGET EnTT AND NOT TARGET EnTT::EnTT)
    add_library(EnTT::EnTT ALIAS EnTT)
endif()

if(NOT TARGET EnTT::EnTT AND NOT TARGET EnTT)
    message(FATAL_ERROR "OpenTS requires EnTT as a third-party dependency.")
endif()

#
# ---------------------------------------------------------
# bgfx (rendering backend)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bgfx.cmake/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/bgfx.cmake is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# bgfx builds shader and texture tools by default; the engine ships neither.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_RENDERER_WEBGPU OFF CACHE BOOL "" FORCE)

# The engine links the static CRT, so its dependencies must agree.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(bgfx.cmake)

# These libraries support the disabled texture tools and are not linked into OpenTS.
set_target_properties(bimg_decode bimg_encode PROPERTIES EXCLUDE_FROM_ALL TRUE)

if(OPENTS_EXPERIMENTAL_CLANG_CL AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # bx treats Clang with the MSVC CRT like a non-x86 compiler and erases
    # __stdcall. Restore it before Windows declarations are parsed.
    foreach(target bx bimg bgfx)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:/FI${PROJECT_SOURCE_DIR}/thirdparty/bx-clang-compat.h>"
        )
    endforeach()

    # The ASTC encoder selects a 64-bit popcount intrinsic when SSE4.2 is enabled,
    # but that intrinsic is unavailable for the Win32 target. Its SSE2 path is portable.
    target_compile_definitions(bimg PRIVATE ASTCENC_POPCNT=0 ASTCENC_SSE=20)
endif()
