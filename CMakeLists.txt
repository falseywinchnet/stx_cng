cmake_minimum_required(VERSION 3.21)
project(stx_cng VERSION 1.0.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
include(cmake/zstd.cmake)

add_library(stx_cng STATIC src/codec.cpp src/cng_encoder.cpp src/cng_decoder.cpp src/collection.cpp $<TARGET_OBJECTS:stx_cng_zstd>)
add_library(stx_cng::stx_cng ALIAS stx_cng)
target_compile_features(stx_cng PUBLIC cxx_std_20)
set_target_properties(stx_cng PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(stx_cng PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Zstandard stays private: its objects are inside libstx_cng and no zstd type or header reaches the public API.
target_include_directories(stx_cng SYSTEM PRIVATE "${STX_CNG_ZSTD_INCLUDE}")
target_link_libraries(stx_cng PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(stx_cng PRIVATE /W4 /WX)
else()
    target_compile_options(stx_cng PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    option(STX_CNG_SANITIZE "Build the library, Zstandard and the tests with the address and undefined-behaviour sanitizers" OFF)
    add_executable(stx_cng_roundtrip tests/roundtrip.cpp)
    add_executable(stx_cng_conformance tests/conformance.cpp)
    add_executable(stx_cng_malformed tests/malformed.cpp)
    foreach(test stx_cng_roundtrip stx_cng_conformance stx_cng_malformed)
        target_link_libraries(${test} PRIVATE stx_cng)
        target_include_directories(${test} SYSTEM PRIVATE "${STX_CNG_ZSTD_INCLUDE}")   # the tests craft tables frames
    endforeach()
    add_test(NAME stx_cng_roundtrip COMMAND stx_cng_roundtrip)
    add_test(NAME stx_cng_conformance COMMAND stx_cng_conformance "${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors")
    add_test(NAME stx_cng_malformed COMMAND stx_cng_malformed)
    if(STX_CNG_SANITIZE)
        foreach(target stx_cng stx_cng_zstd stx_cng_roundtrip stx_cng_conformance stx_cng_malformed)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
        endforeach()
        foreach(target stx_cng_roundtrip stx_cng_conformance stx_cng_malformed)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
endif()

install(TARGETS stx_cng EXPORT stx_cngTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE LICENSE-zstd.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/stx_cng)
install(EXPORT stx_cngTargets NAMESPACE stx_cng:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_cng)
configure_package_config_file(cmake/stx_cngConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_cng)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stx_cngConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_cng)
