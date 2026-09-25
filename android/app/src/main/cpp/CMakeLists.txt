cmake_minimum_required(VERSION 3.22.1)
project(LCSRecompAndroid LANGUAGES C CXX)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

set(PSPRECOMP_CORE_SOURCES
    "${REPO_ROOT}/src/decoder.cpp"
    "${REPO_ROOT}/src/deflate.cpp"
    "${REPO_ROOT}/src/elf32.cpp"
    "${REPO_ROOT}/src/guest_memory.cpp"
    "${REPO_ROOT}/src/nid_registry.cpp"
    "${REPO_ROOT}/src/program_analysis.cpp"
    "${REPO_ROOT}/src/runtime.cpp"
    "${REPO_ROOT}/src/sha256.cpp"
)

add_library(psprecomp_core STATIC ${PSPRECOMP_CORE_SOURCES})
target_include_directories(psprecomp_core PUBLIC "${REPO_ROOT}/include")
target_compile_options(psprecomp_core PRIVATE -O2 -fexceptions -frtti)
target_compile_definitions(psprecomp_core PUBLIC
    PSPRECOMP_AOT_ASSUME_NO_WRITE_WATCH=1
    PSPRECOMP_AOT_PRODUCTION_FASTPATHS=1
)

file(GLOB LCS_GENERATED CONFIGURE_DEPENDS "${REPO_ROOT}/lcs/generated/*.cpp")
add_library(lcs_generated STATIC ${LCS_GENERATED})
target_include_directories(lcs_generated PRIVATE
    "${REPO_ROOT}/include"
    "${REPO_ROOT}/lcs/host"
)
# Iteration builds use O1 without debug info: the generated AOT source is ~96 MB.
# Once the Android port is stable, release builds can switch this back to O2.
target_compile_options(lcs_generated PRIVATE -O1 -g0 -fexceptions -frtti)
target_compile_definitions(lcs_generated PRIVATE
    PSPRECOMP_AOT_ASSUME_NO_WRITE_WATCH=1
    PSPRECOMP_AOT_PRODUCTION_FASTPATHS=1
)

file(GLOB LCS_HOST CONFIGURE_DEPENDS "${REPO_ROOT}/lcs/host/*.cpp")
list(REMOVE_ITEM LCS_HOST
    "${REPO_ROOT}/lcs/host/display_window.cpp"
    "${REPO_ROOT}/lcs/host/lcs_media_decoder.cpp"
    "${REPO_ROOT}/lcs/host/main.cpp"
)

add_library(lcs_main_obj OBJECT "${REPO_ROOT}/lcs/host/main.cpp")
target_include_directories(lcs_main_obj PRIVATE
    "${REPO_ROOT}/include"
    "${REPO_ROOT}/lcs/host"
)
target_compile_definitions(lcs_main_obj PRIVATE
    main=lcs_android_entry
    PSPRECOMP_AOT_ASSUME_NO_WRITE_WATCH=1
    PSPRECOMP_AOT_PRODUCTION_FASTPATHS=1
)
target_compile_options(lcs_main_obj PRIVATE -O2 -fexceptions -frtti)

add_library(lcsrecomp SHARED
    ${LCS_HOST}
    "${CMAKE_CURRENT_LIST_DIR}/android_bridge.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/display_window_android.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lcs_media_decoder_stub.cpp"
    $<TARGET_OBJECTS:lcs_main_obj>
)

target_include_directories(lcsrecomp PRIVATE
    "${REPO_ROOT}/include"
    "${REPO_ROOT}/lcs/host"
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_compile_definitions(lcsrecomp PRIVATE
    PSPRECOMP_AOT_ASSUME_NO_WRITE_WATCH=1
    PSPRECOMP_AOT_PRODUCTION_FASTPATHS=1
)
target_compile_options(lcsrecomp PRIVATE -O2 -fexceptions -frtti)

find_library(ANDROID_LIB android)
find_library(LOG_LIB log)
find_library(EGL_LIB EGL)
find_library(GLES_LIB GLESv3)

target_link_libraries(lcsrecomp PRIVATE
    lcs_generated
    psprecomp_core
    ${ANDROID_LIB}
    ${LOG_LIB}
    ${EGL_LIB}
    ${GLES_LIB}
)
