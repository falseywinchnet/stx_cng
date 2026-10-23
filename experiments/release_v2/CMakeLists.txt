cmake_minimum_required(VERSION 3.20)
project(stx_cng VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)
find_package(ZLIB REQUIRED)
find_path(ZSTD_INCLUDE_DIR zstd.h REQUIRED)
find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
# The bounded context uses Zstandard's advanced API. Pin the dependency version;
# callers must supply matching 1.5.7 headers and library, including at runtime.
file(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h" ZSTD_VERSION_DEFINES REGEX "^#define ZSTD_VERSION_(MAJOR|MINOR|RELEASE) +[0-9]+")
if(NOT ZSTD_VERSION_DEFINES MATCHES "ZSTD_VERSION_MAJOR +1(;|$)" OR
   NOT ZSTD_VERSION_DEFINES MATCHES "ZSTD_VERSION_MINOR +5(;|$)" OR
   NOT ZSTD_VERSION_DEFINES MATCHES "ZSTD_VERSION_RELEASE +7(;|$)")
    message(FATAL_ERROR "stx_cng 0.1 requires matching Zstandard 1.5.7 headers and library")
endif()
add_library(stx_cng STATIC src/codec.cpp src/collection.cpp src/prediction_parse.cpp
    src/prediction_restore.cpp src/raw_restore.cpp src/direct_words.cpp)
add_library(stx_cng::stx_cng ALIAS stx_cng)
target_compile_features(stx_cng PUBLIC cxx_std_20)
set_target_properties(stx_cng PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(stx_cng PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(stx_cng PRIVATE "${ZSTD_INCLUDE_DIR}")
target_link_libraries(stx_cng PRIVATE "${ZSTD_LIBRARY}" "${ZLIB_LIBRARIES}")
# Apple's SDK headers are already in the compiler's system search path. Adding
# SDK/usr/include with -isystem ahead of libc++ breaks its C-header wrappers.
if(NOT APPLE)
    target_include_directories(stx_cng PRIVATE "${ZLIB_INCLUDE_DIRS}")
endif()
if(MSVC)
    target_compile_options(stx_cng PRIVATE /W4 /WX)
else()
    target_compile_options(stx_cng PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING)
    add_executable(stx_cng_roundtrip tests/roundtrip.cpp)
    target_link_libraries(stx_cng_roundtrip PRIVATE stx_cng)
    add_test(NAME stx_cng_roundtrip COMMAND stx_cng_roundtrip)
endif()
install(TARGETS stx_cng ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
