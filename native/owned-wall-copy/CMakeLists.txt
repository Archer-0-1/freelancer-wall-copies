cmake_minimum_required(VERSION 3.20)
project(FreelancerOwnedWallCopy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

get_filename_component(FREELANCER_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ZHMMODSDK_DEVPKG_DIR "${FREELANCER_PROJECT_ROOT}/research/native-spawn-v4.1.1/devkit/unpacked" CACHE PATH "Verified ZHMModSDK v4.1.1 DevPkg root")
if(NOT EXISTS "${ZHMMODSDK_DEVPKG_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ZHMModSDK DevPkg not found at ${ZHMMODSDK_DEVPKG_DIR}")
endif()

# This imports the SDK headers and release libraries. Do not call zhm-mod.cmake:
# its install helper also copies ZHMModSDK.dll into the game Retail directory.
add_subdirectory("${ZHMMODSDK_DEVPKG_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/zhm-sdk")

add_library(FreelancerOwnedWallCopy SHARED src/Plugin.cpp)
target_include_directories(FreelancerOwnedWallCopy BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
target_link_libraries(FreelancerOwnedWallCopy PRIVATE ZHMModSDK implot user32)
if(MSVC)
    target_compile_options(FreelancerOwnedWallCopy PRIVATE /utf-8)
endif()
set_target_properties(FreelancerOwnedWallCopy PROPERTIES OUTPUT_NAME "FreelancerOwnedWallCopy")

add_executable(FreelancerCopyPolicyTests tests/CopyPolicyTests.cpp)
target_compile_features(FreelancerCopyPolicyTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(FreelancerCopyPolicyTests PRIVATE /utf-8)
endif()


