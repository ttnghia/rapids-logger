# =============================================================================
# Copyright (c) 2024, NVIDIA CORPORATION.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations under
# the License.
# =============================================================================

cmake_minimum_required(VERSION 3.30.4 FATAL_ERROR)

include(rapids_config.cmake)
include(rapids-cmake)
include(rapids-cpm)
include(rapids-export)

# Fetch fmt
function(get_fmt)
  set(to_install OFF)
  if(INSTALL_EXPORT_SET IN_LIST ARGN)
    set(to_install ON)
  endif()

  include("${rapids-cmake-dir}/cpm/find.cmake")
  set(version 11.0.2)
  rapids_cpm_find(
    fmt "${version}" ${ARGN}
    GLOBAL_TARGETS fmt::fmt fmt::fmt-header-only
    CPM_ARGS
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG "v${version}"
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL ON
    OPTIONS "FMT_INSTALL ${to_install}" "CMAKE_POSITION_INDEPENDENT_CODE ON"
  )

  # Propagate up variables that CPMFindPackage provide
  set(fmt_SOURCE_DIR
      "${fmt_SOURCE_DIR}"
      PARENT_SCOPE
  )
  set(fmt_BINARY_DIR
      "${fmt_BINARY_DIR}"
      PARENT_SCOPE
  )
  set(fmt_ADDED
      "${fmt_ADDED}"
      PARENT_SCOPE
  )
  set(fmt_VERSION
      ${version}
      PARENT_SCOPE
  )
endfunction()

# Fetch spdlog
function(get_spdlog)
  set(options)
  set(one_value FMT_OPTION BUILD_EXPORT_SET INSTALL_EXPORT_SET)
  set(multi_value)
  cmake_parse_arguments(_RAPIDS "${options}" "${one_value}" "${multi_value}" ${ARGN})

  # Fix up _RAPIDS_UNPARSED_ARGUMENTS to have EXPORT_SETS as this is need for rapids_cpm_find. Also
  # propagate the user provided build and install export sets.
  if(_RAPIDS_INSTALL_EXPORT_SET)
    list(APPEND _RAPIDS_UNPARSED_ARGUMENTS INSTALL_EXPORT_SET ${_RAPIDS_INSTALL_EXPORT_SET})
  endif()
  if(_RAPIDS_BUILD_EXPORT_SET)
    list(APPEND _RAPIDS_UNPARSED_ARGUMENTS BUILD_EXPORT_SET ${_RAPIDS_BUILD_EXPORT_SET})
  endif()

  set(to_install OFF)
  if(_RAPIDS_INSTALL_EXPORT_SET)
    set(to_install ON)
  endif()

  # If the option wasn't passed to the command, default to header only fmt
  if(NOT _RAPIDS_FMT_OPTION)
    set(_RAPIDS_FMT_OPTION "EXTERNAL_FMT_HO")
  endif()

  if(_RAPIDS_FMT_OPTION STREQUAL "BUNDLED")
    set(spdlog_fmt_option "")
  elseif(_RAPIDS_FMT_OPTION STREQUAL "EXTERNAL_FMT")
    set(spdlog_fmt_option "SPDLOG_FMT_EXTERNAL ON")
    set(spdlog_fmt_target fmt::fmt)
  elseif(_RAPIDS_FMT_OPTION STREQUAL "EXTERNAL_FMT_HO")
    set(spdlog_fmt_option "SPDLOG_FMT_EXTERNAL_HO ON")
    set(spdlog_fmt_target fmt::fmt-header-only)
  elseif(_RAPIDS_FMT_OPTION STREQUAL "STD_FORMAT")
    set(spdlog_fmt_option "SPDLOG_USE_STD_FORMAT ON")
  else()
    message(
      FATAL_ERROR
        "Invalid option used for FMT_OPTION, got: ${_RAPIDS_FMT_OPTION}, expected one of: 'BUNDLED', 'EXTERNAL_FMT', 'EXTERNAL_FMT_HO', 'STD_FORMAT'"
    )
  endif()

  if(_RAPIDS_FMT_OPTION STREQUAL "EXTERNAL_FMT" OR _RAPIDS_FMT_OPTION STREQUAL "EXTERNAL_FMT_HO")
    include("${rapids-cmake-dir}/cpm/fmt.cmake")

    # Using `spdlog_ROOT` needs to cause any internal find calls in `spdlog-config.cmake` to first
    # search beside it before looking globally.
    list(APPEND fmt_ROOT ${spdlog_ROOT})

    get_fmt(${_RAPIDS_UNPARSED_ARGUMENTS})
  endif()

  include("${rapids-cmake-dir}/cpm/find.cmake")
  set(version 1.14.1)
  rapids_cpm_find(
    spdlog "${version}" ${_RAPIDS_UNPARSED_ARGUMENTS}
    GLOBAL_TARGETS spdlog::spdlog spdlog::spdlog_header_only
    CPM_ARGS
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG "v${version}"
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL ON
    OPTIONS "SPDLOG_INSTALL ${to_install}" "${spdlog_fmt_option}"
  )

  # Propagate up variables that CPMFindPackage provide
  set(spdlog_SOURCE_DIR
      "${spdlog_SOURCE_DIR}"
      PARENT_SCOPE
  )
  set(spdlog_BINARY_DIR
      "${spdlog_BINARY_DIR}"
      PARENT_SCOPE
  )
  set(spdlog_ADDED
      "${spdlog_ADDED}"
      PARENT_SCOPE
  )
  set(spdlog_VERSION
      ${version}
      PARENT_SCOPE
  )
  set(spdlog_fmt_target
      ${spdlog_fmt_target}
      PARENT_SCOPE
  )
endfunction()

file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" _version)
string(STRIP "${_version}" _version)
string(REGEX MATCH "[0-9]+\.[0-9]+\.[0-9]+" _version "${_version}")

project(
  RAPIDS_LOGGER
  VERSION "${_version}"
  LANGUAGES CXX
)

rapids_cmake_build_type(Release)

rapids_cpm_init()

option(BUILD_TESTS "Configure CMake to build tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

include(CMakeDependentOption)
# We cannot hide all spdlog symbols if we are building a static library.
cmake_dependent_option(
  RAPIDS_LOGGER_HIDE_ALL_SPDLOG_SYMBOLS
  "Build and link to spdlog in a way that maximizes all symbol hiding" ON "BUILD_SHARED_LIBS" OFF
)

# If we are hiding all spdlog symbols then we need to use the bundled fmt library, so this option is
# only configurable if we are not hiding those symbols.
cmake_dependent_option(
  RAPIDS_LOGGER_FMT_OPTION "The fmt option to use when building spdlog." "EXTERNAL_FMT_HO"
  "NOT RAPIDS_LOGGER_HIDE_ALL_SPDLOG_SYMBOLS" "BUNDLED"
)

# TODO: For Spark-RAPIDS we will have to support building this as a static library. We may need some
# additional testing to make sure that will work.
add_library(rapids_logger src/logger.cpp)
add_library(rapids_logger::rapids_logger ALIAS rapids_logger)
target_include_directories(
  rapids_logger PUBLIC "$<BUILD_INTERFACE:${RAPIDS_LOGGER_SOURCE_DIR}/include>"
                       "$<INSTALL_INTERFACE:include>"
)
set_target_properties(
  rapids_logger
  PROPERTIES BUILD_RPATH "\$ORIGIN"
             INSTALL_RPATH "\$ORIGIN"
             # set target compile options
             CXX_STANDARD 17
             CXX_STANDARD_REQUIRED ON
             CXX_VISIBILITY_PRESET hidden
             POSITION_INDEPENDENT_CODE ON
)

# Get spdlog
if(RAPIDS_LOGGER_HIDE_ALL_SPDLOG_SYMBOLS)
  set(CPM_DOWNLOAD_spdlog ON)
  get_spdlog(
    FMT_OPTION ${RAPIDS_LOGGER_FMT_OPTION} CPM_ARGS OPTIONS "BUILD_SHARED_LIBS OFF"
    "SPDLOG_BUILD_SHARED OFF"
  )
  set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_options(rapids_logger PRIVATE "LINKER:--exclude-libs,libspdlog")
else()
  get_spdlog(
    FMT_OPTION
    ${RAPIDS_LOGGER_FMT_OPTION}
    BUILD_EXPORT_SET
    rapids-logger-exports
    INSTALL_EXPORT_SET
    rapids-logger-exports
    CPM_ARGS
    OPTIONS
    "BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS}"
    "SPDLOG_BUILD_SHARED ${BUILD_SHARED_LIBS}"
  )
  if(spdlog_ADDED)
    set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()
target_link_libraries(rapids_logger PRIVATE spdlog::spdlog)

if(BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

rapids_cmake_install_lib_dir(lib_dir)
install(
  TARGETS rapids_logger
  DESTINATION ${lib_dir}
  EXPORT rapids-logger-exports
)
install(DIRECTORY ${RAPIDS_LOGGER_SOURCE_DIR}/include/rapids_logger
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Both the function to create macros and the template must be installed so that they are available
# to use from the installed package. TODO: This file should probably go into share and not be right
# alongside the CMake file in lib, but this is easiest for now
install(FILES ${CMAKE_CURRENT_LIST_DIR}/cmake/logger_macros.hpp.in
        DESTINATION "${lib_dir}/cmake/rapids_logger"
)
install(FILES ${CMAKE_CURRENT_LIST_DIR}/cmake/create_logger_macros.cmake
        DESTINATION "${lib_dir}/cmake/rapids_logger"
)

# cmake-lint: disable=E1126
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/logger_macros.hpp.in"
     "${CMAKE_CURRENT_BINARY_DIR}/logger_macros.hpp.in"
)
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/create_logger_macros.cmake"
     "${CMAKE_CURRENT_BINARY_DIR}/create_logger_macros.cmake"
)
# For e.g. CPM builds from source we need to make sure that the function is available.
include(cmake/create_logger_macros.cmake)

set(doc_string
    [=[
Provide targets for the rapids-logger library.

rapids-logger provides an ABI stable interface to spdlog-like logging that can
be safely embedded into complex environments where exposing spdlog symbols or
having it as a public dependency makes stable environment difficult to create
or maintain safely.

Imported Targets
^^^^^^^^^^^^^^^^

If rapids_logger is found, this module defines the following IMPORTED GLOBAL
targets:

 rapids_logger::rapids_logger             - The rapids_logger library.

    ]=]
)

set(code_string
    [=[
include("${CMAKE_CURRENT_LIST_DIR}/create_logger_macros.cmake")
]=]
)

rapids_export(
  INSTALL rapids_logger
  EXPORT_SET rapids-logger-exports
  GLOBAL_TARGETS rapids_logger
  NAMESPACE rapids_logger::
  DOCUMENTATION doc_string
  FINAL_CODE_BLOCK code_string
)
rapids_export(
  BUILD rapids_logger
  EXPORT_SET rapids-logger-exports
  GLOBAL_TARGETS rapids_logger
  NAMESPACE rapids_logger::
  DOCUMENTATION doc_string
  FINAL_CODE_BLOCK code_string
)
