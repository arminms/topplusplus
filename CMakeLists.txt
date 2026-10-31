# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Armin Sobhani
#
cmake_minimum_required(VERSION 3.25.2...4.4.0)

## life is about choices...
#
option(TOPPP_ENABLE_TESTS "Enable the unit tests ?" OFF)
option(TOPPP_ENABLE_STATIC "Enable static linking ?" OFF)

## vcpkg manifest features
#
if(${TOPPP_ENABLE_TESTS})
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

## finally our project...
#
project(
  top++
  VERSION 0.5.0
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

## static linking (single self-contained binary)
#
# Installed packages are usually shared libraries, so ftxui and lfreist-hwinfo
# are built from source as static libraries instead of being looked up.
#
if(TOPPP_ENABLE_STATIC)
  set(CMAKE_DISABLE_FIND_PACKAGE_ftxui ON)
  set(CMAKE_DISABLE_FIND_PACKAGE_lfreist-hwinfo ON)
  set(BUILD_SHARED_LIBS OFF)
  set(HWINFO_STATIC ON)
  # nothing of the static dependencies is needed once they are linked in
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(TOPPP_FETCH_EXCLUDE_FROM_ALL EXCLUDE_FROM_ALL)
  endif()
  # MSVC: link the static runtime (/MT) so no Visual C++ redistributable is needed
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

## check for ftxui
#
find_package(
  ftxui CONFIG
  HINTS $ENV{HOME} $ENV{HOME}/.local /usr/local /opt/local /opt
)
if(NOT ftxui_FOUND)
    message(STATUS "Fetching ftxui library...")
    # ftxui is linked statically, so there is nothing of it to install
    set(FTXUI_ENABLE_INSTALL OFF)
    FetchContent_Declare(
      ftxui
      GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
      GIT_TAG        v7.0.3
      ${TOPPP_FETCH_EXCLUDE_FROM_ALL}
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
      ${TOPPP_FETCH_EXCLUDE_FROM_ALL}
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

## link the C++ runtime statically as well
#
# MSVC is covered by CMAKE_MSVC_RUNTIME_LIBRARY above. macOS cannot link its
# system libraries statically, so only the dependencies are static there. On
# Linux libc stays dynamic: hwinfo loads the vendor GPU libraries (NVML,
# OpenCL...) with dlopen(), which a fully static glibc binary cannot do.
#
if(TOPPP_ENABLE_STATIC AND NOT MSVC)
  if(WIN32)
    target_link_options(top++ PRIVATE -static)
  elseif(NOT APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX -static-libgcc TOPPP_HAS_STATIC_LIBGCC)
    check_linker_flag(CXX -static-libstdc++ TOPPP_HAS_STATIC_LIBSTDCXX)
    if(TOPPP_HAS_STATIC_LIBGCC)
      target_link_options(top++ PRIVATE -static-libgcc)
    endif()
    if(TOPPP_HAS_STATIC_LIBSTDCXX)
      target_link_options(top++ PRIVATE -static-libstdc++)
    endif()
  endif()
endif()

## installation
#
# A static build has no libraries to locate at run time, so it gets no RPATH.
# Otherwise the hwinfo libraries are often installed outside the default linker
# search path (e.g. ~/.local/lib), so keep their directories in the installed
# RPATH. $ORIGIN/../lib covers shared libraries installed next to top++ itself.
if(TOPPP_ENABLE_STATIC)
  set_target_properties(top++ PROPERTIES SKIP_BUILD_RPATH TRUE)
else()
  set_target_properties(
    top++ PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
  )
endif()
install(TARGETS top++ RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
