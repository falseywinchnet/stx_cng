cmake_minimum_required(VERSION 3.21)
project(stx_cng VERSION 0.2.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CTest)
find_package(ZLIB REQUIRED)
include(CMakePackageConfigHelpers)
add_subdirectory(stx_cngcompress)
add_library(stx_cng STATIC src/codec.cpp src/collection.cpp src/prediction_parse.cpp
    src/prediction_restore.cpp src/raw_restore.cpp src/direct_words.cpp)
add_library(stx_cng::stx_cng ALIAS stx_cng)
target_compile_features(stx_cng PUBLIC cxx_std_20)
set_target_properties(stx_cng PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(stx_cng PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(stx_cng PRIVATE stx_cngcompress::stx_cngcompress
    "$<BUILD_INTERFACE:${ZLIB_LIBRARIES}>" "$<INSTALL_INTERFACE:ZLIB::ZLIB>")
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
install(TARGETS stx_cng EXPORT stx_cngTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT stx_cngTargets NAMESPACE stx_cng:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_cng)
configure_package_config_file(cmake/stx_cngConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_cng)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_cng)
