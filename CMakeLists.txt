cmake_minimum_required(VERSION 3.16)

# Read version from VERSION file (single source of truth)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RESEQ_VERSION_RAW)
string(STRIP "${RESEQ_VERSION_RAW}" RESEQ_VERSION)
string(REPLACE "." ";" RESEQ_VERSION_LIST ${RESEQ_VERSION})
list(GET RESEQ_VERSION_LIST 0 RESEQ_VERSION_MAJOR)
list(GET RESEQ_VERSION_LIST 1 RESEQ_VERSION_MINOR)
list(GET RESEQ_VERSION_LIST 2 RESEQ_VERSION_PATCH)

project(reseq LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
      "Choose the type of build, options are: Debug Release
RelWithDebInfo MinSizeRel."
      FORCE)
endif(NOT CMAKE_BUILD_TYPE)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Compression packages
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

# Add boost
cmake_policy(SET CMP0057 NEW)
find_package(Boost 1.48.0 REQUIRED filesystem math_c99 math_c99f math_c99l math_tr1 math_tr1f math_tr1l program_options serialization system)

# NLopt via FetchContent (or system install)
find_package(NLopt QUIET)
if(NOT NLopt_FOUND)
  FetchContent_Declare(
    nlopt
    GIT_REPOSITORY https://github.com/stevengj/nlopt.git
    GIT_TAG v2.9.1
  )
  set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
  set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
  set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
  set(NLOPT_GUILE OFF CACHE BOOL "" FORCE)
  set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(nlopt)
endif()

# Add SeqAn as INTERFACE target
LIST(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/seqan/util/cmake/")
find_package(SeqAn REQUIRED)
add_library(vendored_seqan INTERFACE)
target_include_directories(vendored_seqan SYSTEM INTERFACE
  "${PROJECT_SOURCE_DIR}/seqan/include"
  ${SEQAN_INCLUDE_DIRS}
)
target_compile_definitions(vendored_seqan INTERFACE ${SEQAN_DEFINITIONS})
separate_arguments(SEQAN_CXX_FLAGS_LIST NATIVE_COMMAND "${SEQAN_CXX_FLAGS}")
target_compile_options(vendored_seqan INTERFACE ${SEQAN_CXX_FLAGS_LIST})
target_link_libraries(vendored_seqan INTERFACE ${SEQAN_LIBRARIES})

# Vendored ROOTPWA utilities as INTERFACE target
add_library(vendored_rootpwa INTERFACE)
target_include_directories(vendored_rootpwa SYSTEM INTERFACE
  "${PROJECT_SOURCE_DIR}/2016-05-15_ROOTPWA/utilities"
)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Version (read from VERSION file above)
# Optional: git describe for dev builds
find_package(Git QUIET)
set(RESEQ_GIT_DESCRIBE "v${RESEQ_VERSION}")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE RESEQ_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
  )
  if(NOT GIT_RESULT EQUAL 0)
    set(RESEQ_GIT_DESCRIBE "v${RESEQ_VERSION}")
  endif()
endif()

# Configure header file to pass CMake settings to code
configure_file(
	"${PROJECT_SOURCE_DIR}/CMakeConfig.h.in"
	"${PROJECT_BINARY_DIR}/CMakeConfig.h"
)

# Enable CTest (must be in root CMakeLists.txt for ctest to discover tests)
enable_testing()

# Subdirectories
add_subdirectory(skewer)
add_subdirectory(reseq)
option(RESEQ_BUILD_PYTHON "Build Python bindings (requires SWIG 3+ and python3-dev)" OFF)
option(CODE_COVERAGE "Enable code coverage instrumentation (GCC only)" OFF)
if(RESEQ_BUILD_PYTHON)
  add_subdirectory(python)
endif()
