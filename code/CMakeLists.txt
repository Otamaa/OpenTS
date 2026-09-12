# Limit the generated solution to supported configurations.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

# OpenTS currently supports 32-bit (x86) builds only.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "OpenTS must be built as 32-bit x86. Reconfigure with -A Win32.")
endif()

#
# ---------------------------------------------------------
# Directories that build as their own project
# ---------------------------------------------------------
#
set(SUBPROJECT_DIRS
    "language"
    "vqalib"
)

#
# ---------------------------------------------------------
# Gather all potential OpenTS files
# ---------------------------------------------------------
#
file(GLOB_RECURSE OPENTS_RAW CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)

#
# ---------------------------------------------------------
# Remove files that belong to a subproject
# ---------------------------------------------------------
#
set(OPENTS_SRC "")
foreach(f ${OPENTS_RAW})
    set(EXCLUDED FALSE)
    foreach(dir ${SUBPROJECT_DIRS})
        if(f MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/${dir}/.*")
            set(EXCLUDED TRUE)
        endif()
    endforeach()

    if(NOT EXCLUDED)
        list(APPEND OPENTS_SRC "${f}")
    endif()
endforeach()

#
# ---------------------------------------------------------
# Create actual OpenTS executable (Win32)
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating '${PROJECT_NAME}' executable...")

add_executable(OpenTS WIN32 ${OPENTS_SRC})
target_compile_features(OpenTS PRIVATE cxx_std_20)

#
# ---------------------------------------------------------
# Output paths
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting binary output paths...")

set_target_properties(OpenTS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"

    # Built under the shipped names, so the executable, the symbol file the debug record
    # names, and the map reach the run directory without a rename.
    OUTPUT_NAME_DEBUG "GameD"
    OUTPUT_NAME_RELEASE "Game"
    PDB_NAME_DEBUG "GameD"
    PDB_NAME_RELEASE "Game"
)

set_target_properties(OpenTS PROPERTIES
    VS_DEBUGGER_COMMAND "${TS_RUN_DIR}/$<TARGET_FILE_NAME:OpenTS>"
    VS_DEBUGGER_WORKING_DIRECTORY "${TS_RUN_DIR}"
)

#
# ---------------------------------------------------------
# Compile flags
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying compiler flags...")

set(OPENTS_COMPILE_OPTIONS

    # ================= DEBUG =================
    $<$<CONFIG:Debug>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /Od /RTC1 /GR $<$<CXX_COMPILER_ID:MSVC>:/MP> /EHsc /Oy- /MTd /Zc:__cplusplus /arch:SSE2 /fp:precise
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /Od /RTC1 $<$<C_COMPILER_ID:MSVC>:/MP> /Oy- /arch:SSE2 /fp:precise
        >
    >

    # ================= RELEASE =================
    $<$<CONFIG:Release>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /O2 /GF /GR $<$<CXX_COMPILER_ID:MSVC>:/MP> /EHsc /MT /Zc:__cplusplus /arch:SSE2 /fp:precise
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /O2 /GF $<$<C_COMPILER_ID:MSVC>:/MP> /arch:SSE2 /fp:precise
        >
    >
)

target_compile_options(OpenTS PRIVATE ${OPENTS_COMPILE_OPTIONS})

#
# ---------------------------------------------------------
# Subprojects
# ---------------------------------------------------------
#
# The VQA player carries its own project, and hands OpenTS its include path along with the
# library, so the movie code is reached the same way from anywhere in the solution.
add_subdirectory(vqalib)

message(STATUS "${PROJECT_NAME}: Adding include paths...")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OPENTS_GENERATED_DIR}"
)

# The generated build stamp has to exist before anything compiles.
add_dependencies(OpenTS OpenTSBuildStamp)

# Only the renderer's own translation unit sees bgfx, so its headers and the shaders it
# embeds are put on that file rather than on the whole target.
set(BGFX_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bgfx")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" PROPERTIES
    INCLUDE_DIRECTORIES
        "${BGFX_ROOT}/include;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bx/include;${BGFX_ROOT}/examples/common/imgui;${CMAKE_SOURCE_DIR}/thirdparty/bgfx.cmake/bimg/include;${CMAKE_SOURCE_DIR}/thirdparty/stb"
    COMPILE_DEFINITIONS
        "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,$<BOOL:${BX_CONFIG_DEBUG}>>;$<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:OPENTS_BUILD_64BIT>"
    COMPILE_OPTIONS "/Zc:preprocessor"
)

# EXTENSION: compiles the postfx shaders (code/shaders/postfx) into per-profile headers,
# then points bgfxbackend.cpp at the directory containing them. AS_HEADERS names each
# generated array to match what BGFX_EMBEDDED_SHADER() in that file expects; see the
# per-profile #include block above the shader's own #include of this project's headers.
#
# BGFX_SHADER_INCLUDE_PATH is bgfx_compile_shaders()'s own -i path for a shader's
# #include <bgfx_shader.sh>. bgfx.cmake references this variable but never sets it
# itself -- it's on the consumer to point it at bgfx's own src directory, and without it
# every shader compiled below fails to parse at its #include line with a bare
# "'declaration' : Expected", since the include silently resolves to nothing.
set(BGFX_SHADER_INCLUDE_PATH "${BGFX_ROOT}/src")

# EXTENSION: s_6_0 (Shader Model 6, compiled via DXC) is only added for a 64-bit build.
# DXC needs dxcompiler.dll/dxil.dll discoverable next to shaderc.exe, which in practice
# tends to actually be there on an x64 toolchain and tends not to be on the x86 one this
# project currently builds as -- s_5_0 (DXBC, via the much more universally available
# d3dcompiler_47.dll) covers D3D11/D3D12 either way, so 32-bit simply doesn't try SM6 at
# all rather than risk the same missing-DXC failure this was written to fix. Matches
# OPENTS_BUILD_64BIT in code/bgfxbackend.cpp, which the embedded-shader registration side
# of this same profile choice depends on; see that file's own comment.
set(OPENTS_SHADER_PROFILES spirv 430 300_es s_5_0)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND OPENTS_SHADER_PROFILES s_6_0)
endif()

set(OPENTS_POSTFX_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/postfx")
set(OPENTS_POSTFX_SHADER_OUTPUT_DIR "${OPENTS_GENERATED_DIR}/postfx-shaders")

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS "${OPENTS_POSTFX_SHADER_DIR}/vs_postfx.sc"
    VARYING_DEF "${OPENTS_POSTFX_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${OPENTS_POSTFX_SHADER_OUTPUT_DIR}"
    OUT_FILES_VAR OPENTS_POSTFX_VERTEX_OUTPUTS
    PROFILES ${OPENTS_SHADER_PROFILES}
    AS_HEADERS
)

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS
        "${OPENTS_POSTFX_SHADER_DIR}/fs_bloom_bright.sc"
        "${OPENTS_POSTFX_SHADER_DIR}/fs_bloom_blur.sc"
        "${OPENTS_POSTFX_SHADER_DIR}/fs_bloom_combine.sc"
        "${OPENTS_POSTFX_SHADER_DIR}/fs_distortwarp.sc"
        "${OPENTS_POSTFX_SHADER_DIR}/fs_atmosphere.sc"
    VARYING_DEF "${OPENTS_POSTFX_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${OPENTS_POSTFX_SHADER_OUTPUT_DIR}"
    OUT_FILES_VAR OPENTS_POSTFX_FRAGMENT_OUTPUTS
    PROFILES ${OPENTS_SHADER_PROFILES}
    AS_HEADERS
)

# The compile commands above only run if something actually depends on their outputs.
add_custom_target(OpenTSPostFXShaders DEPENDS ${OPENTS_POSTFX_VERTEX_OUTPUTS} ${OPENTS_POSTFX_FRAGMENT_OUTPUTS})
add_dependencies(OpenTS OpenTSPostFXShaders)

set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${OPENTS_POSTFX_SHADER_OUTPUT_DIR}"
)

# EXTENSION: the GPU beam shaders (code/shaders/gpubeam), compiled the same way and added
# to the same include path bgfxbackend.cpp already has for the postfx headers above.
set(OPENTS_GPUBEAM_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gpubeam")
set(OPENTS_GPUBEAM_SHADER_OUTPUT_DIR "${OPENTS_GENERATED_DIR}/gpubeam-shaders")

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS "${OPENTS_GPUBEAM_SHADER_DIR}/vs_gpubeam.sc"
    VARYING_DEF "${OPENTS_GPUBEAM_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${OPENTS_GPUBEAM_SHADER_OUTPUT_DIR}"
    OUT_FILES_VAR OPENTS_GPUBEAM_VERTEX_OUTPUTS
    PROFILES ${OPENTS_SHADER_PROFILES}
    AS_HEADERS
)

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS
        "${OPENTS_GPUBEAM_SHADER_DIR}/fs_gpubeam.sc"
        "${OPENTS_GPUBEAM_SHADER_DIR}/fs_gpubeam_distort.sc"
        "${OPENTS_GPUBEAM_SHADER_DIR}/fs_anim_overlay.sc"
        "${OPENTS_GPUBEAM_SHADER_DIR}/fs_particle.sc"
    VARYING_DEF "${OPENTS_GPUBEAM_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${OPENTS_GPUBEAM_SHADER_OUTPUT_DIR}"
    OUT_FILES_VAR OPENTS_GPUBEAM_FRAGMENT_OUTPUTS
    PROFILES ${OPENTS_SHADER_PROFILES}
    AS_HEADERS
)

add_custom_target(OpenTSGPUBeamShaders DEPENDS ${OPENTS_GPUBEAM_VERTEX_OUTPUTS} ${OPENTS_GPUBEAM_FRAGMENT_OUTPUTS})
add_dependencies(OpenTS OpenTSGPUBeamShaders)

set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${OPENTS_GPUBEAM_SHADER_OUTPUT_DIR}"
)

# bx rewrites __stdcall while its headers are being parsed by clang-cl.  Force the
# compatibility header into the renderer translation unit as well as bx/bgfx so the
# MSVC standard-library headers that follow still see the Win32 calling convention.
if(OPENTS_EXPERIMENTAL_CLANG_CL AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/bgfxbackend.cpp" APPEND PROPERTY
        COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/thirdparty/bx-clang-compat.h"
    )
endif()

message(STATUS "${PROJECT_NAME}: Adding compilier definitions...")
target_compile_definitions(OpenTS PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    NOMINMAX

    # Compiles Blowfish into the binary instead of reaching it through the COM object in
    # blowfish.dll. It decides the layout of BlowfishEngine, so every translation unit has
    # to agree on it and it belongs on the compile line rather than in a header.
    NO_BLOWFISH_DLL
)

#
# ---------------------------------------------------------
# Windows libraries and linker options
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying linker options...")

# Required libraries:
#  Comctl32.lib
#  DbgHelp.lib
#  DSound.lib
#  Version.lib
#  Winmm.lib
#  wsock32.lib
#  + all Win32 libs
#

target_link_libraries(OpenTS PRIVATE
    VQALib

    bgfx
    bx
    bimg
    miniaudio
    comctl32
    dbghelp
    iphlpapi
    psapi
    version
    winmm
    wsock32
    kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32
    ole32 oleaut32 uuid odbc32 odbccp32
)

if(MSVC)
    target_link_options(OpenTS PRIVATE
        /SUBSYSTEM:WINDOWS
        /DEBUG
        /MAP
        # Raises the user address space from 2GB to 4GB under 64-bit Windows. Pointers may
        # then have the high bit set, so anything holding one in a signed integer must only
        # round-trip the bits and never compare or scale them.
        /LARGEADDRESSAWARE
        # Declares the process per-monitor DPI aware, so a scaled desktop does not blur
        # the frame by stretching the whole window.
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/sun.manifest"
        /MANIFEST:EMBED
    )
endif()

#
# ---------------------------------------------------------
# Visual Studio source groups
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting up Visual Studio filters...")

# Init Source Files (_*.cpp)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.cpp$")
        source_group("Source Files\\Init Source Files" FILES ${f})
    endif()
endforeach()

# Init Header Files (_*.h)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.h$")
        source_group("Header Files\\Init Header Files" FILES ${f})
    endif()
endforeach()

# Define Header Files (*.hh)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.hh$")
        source_group("Header Files\\Define Header Files" FILES ${f})
    endif()
endforeach()

# Resource Files
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.(ico|cur|bmp|gif|jpe?g|rc)$")
        source_group("Resource Files" FILES ${f})
    endif()
endforeach()

# List of all interface filenames (headers + C stubs)
set(INTERFACE_FILES
    iblockci.h       iblockci_i.c
    iblowfish.h      iblowfish_i.c
    iflyctrl.h       iflyctrl_i.c
    ilinkstm.h
    iloco.h          iloco_i.c
    ilocos.h         ilocos_i.c
    ipiggy.h         ipiggy_i.c
    isun.h           isun_i.c
)

# Convert to full paths relative to source dir
set(FULL_INTERFACE_FILES "")
foreach(f IN LISTS INTERFACE_FILES)
    list(APPEND FULL_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
endforeach()

# Add them to the "Interface Files" group and exclude *_i.c from build
foreach(f IN LISTS FULL_INTERFACE_FILES)

    # Put into VS filter
    source_group("Interface Files" FILES "${f}")

    # Exclude *_i.c from build, but DO NOT mark headers as header-only
    if(f MATCHES "_i\\.c$")
        set_source_files_properties("${f}" PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()

endforeach()

# General source files
source_group("Source Files" REGULAR_EXPRESSION ".*\\.(c|cpp)$")
source_group("Header Files" REGULAR_EXPRESSION ".*\\.(h|hpp)$")

#
# ---------------------------------------------------------
# Populate the local run directory
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating post-build commands...")
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${TS_RUN_DIR}"
)

add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenTS>"
        "${TS_RUN_DIR}"
)

# Copy the linker-generated .pdb alongside the exe
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PDB_FILE:OpenTS>"
        "${TS_RUN_DIR}"
)

# Copy the linker-generated .map (sits next to the exe, no GenEx for it)
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:OpenTS>/$<TARGET_FILE_BASE_NAME:OpenTS>.map"
        "${TS_RUN_DIR}"
)

#
# ---------------------------------------------------------
# Visual Studio startup project
# ---------------------------------------------------------
#
set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT OpenTS)

# ---------------------------------------------------------
# Add the .natvis file for Visual Studio debugging
# ---------------------------------------------------------
set(NATVIS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sun.natvis")
source_group("Natvis Files" FILES ${NATVIS_FILE})
target_sources(OpenTS PRIVATE ${NATVIS_FILE})
target_link_options(OpenTS PRIVATE "/NATVIS:${NATVIS_FILE}")
