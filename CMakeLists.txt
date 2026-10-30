# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Armin Sobhani
#
cmake_minimum_required(VERSION 3.25.2...4.4.0)

## life is about choices...
#
option(TOPPP_ENABLE_TESTS "Enable the unit tests ?" OFF)

## vcpkg manifest features
#
if(${TOPPP_ENABLE_TESTS})
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

## finally our project...
#
project(
  top++
  VERSION 0.3.0
  LANGUAGES CXX
  DESCRIPTION "C++23 system monitor tool"
  HOMEPAGE_URL "https://github.com/arminms/top++"
)

## prevent in-source builds
#
if (CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

## necessary modules
#
include(GNUInstallDirs)
include(FetchContent)

## check for ftxui
#
find_package(
  ftxui CONFIG
  HINTS $ENV{HOME} $ENV{HOME}/.local /usr/local /opt/local /opt
)
if(NOT ftxui_FOUND)
    message(STATUS "Fetching ftxui library...")
    FetchContent_Declare(
      ftxui
      GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
      GIT_TAG        v7.0.3
    )
    FetchContent_MakeAvailable(ftxui)
endif()

## check for hwinfo
#
find_package(
  lfreist-hwinfo CONFIG
  HINTS $ENV{HOME} $ENV{HOME}/.local /usr/local /opt/local /opt
)
if(NOT lfreist-hwinfo_FOUND)
    message(STATUS "Fetching lfreist-hwinfo library...")
    FetchContent_Declare(
      hwinfo
      GIT_REPOSITORY https://github.com/lfreist/hwinfo
      GIT_TAG        main
    )
    FetchContent_MakeAvailable(hwinfo)
endif()

## add unit tests
#
if(${TOPPP_ENABLE_TESTS})
  enable_testing()
  add_subdirectory(test)
endif()

add_executable(top++ src/top++.cpp)
target_link_libraries(top++ PRIVATE ftxui::ftxui lfreist-hwinfo::hwinfo)
target_compile_features(top++ INTERFACE cxx_std_23)