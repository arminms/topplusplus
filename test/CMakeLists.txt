# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Armin Sobhani
#
find_package(
  Catch2 3
  HINTS $ENV{HOME} $ENV{HOME}/.local /usr/local /opt/local /opt
)
if(NOT Catch2_FOUND)
  message(STATUS "Fetching Catch2 library...")
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
  )
  FetchContent_MakeAvailable(catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
include(Catch)

## function to add unit tests
#
function(add_unit_tests TEST_NAME TEST_SOURCE)
  get_filename_component(TEST_TARGET ${TEST_SOURCE} NAME_WE)
  add_executable(${TEST_TARGET} ${TEST_SOURCE})
  target_link_libraries(${TEST_TARGET} PRIVATE Catch2::Catch2WithMain lfreist-hwinfo::hwinfo)
  target_include_directories(${TEST_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
endfunction()

## defining unit tests
#
add_unit_tests("circular_buffer_64" test_circular_buffer.cpp
  TEST_PREFIX "[circular_buffer_64] "
  )
add_unit_tests("monitor" test_monitor.cpp
  TEST_PREFIX "[monitor] "
  )

## finally adding unit tests
#
catch_discover_tests(test_circular_buffer)
catch_discover_tests(test_monitor)
