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
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
SET( BOOST_LIBRARY_FLAGS "-lboost_serialization -lboost_program_options -lboost_filesystem -lboost_system" )
# Suppress warnings of deprecated std::auto_ptr used in boost
SET( BOOST_LIBRARY_FLAGS "${BOOST_LIBRARY_FLAGS} -Wno-deprecated-declarations" )
SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${BOOST_LIBRARY_FLAGS}" )
# Fix FindBoost issue with not giving ${Boost_LIBRARY_DIRS}
if(NOT $ENV{BOOST_ROOT} STREQUAL "")
	link_directories("$ENV{BOOST_ROOT}/lib/")
endif()
if(NOT $ENV{BOOST_LIBRARYDIR} STREQUAL "")
	link_directories($ENV{BOOST_LIBRARYDIR})
endif()

# Add NLopt
SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wdeprecated-declarations" )
LIST(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/nlopt/cmake/")
add_subdirectory(nlopt)
include_directories("${CMAKE_BINARY_DIR}/nlopt/src/api/")

# Add SeqAn
SET(SEQAN_INCLUDE_PATH "${PROJECT_SOURCE_DIR}/seqan/include")
include_directories(${SEQAN_INCLUDE_PATH})

LIST(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/seqan/util/cmake/")
find_package(SeqAn REQUIRED)
include_directories(${SEQAN_INCLUDE_DIRS})
add_definitions(${SEQAN_DEFINITIONS})
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SEQAN_CXX_FLAGS}")

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

# Compiler flags
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fext-numeric-literals")
endif()
link_libraries(rt)

# Include directories
include_directories("${PROJECT_BINARY_DIR}")
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}/reseq")
include_directories("${PROJECT_SOURCE_DIR}/2016-05-15_ROOTPWA/utilities")

# Subdirectories
add_subdirectory(skewer)
add_subdirectory(reseq)
option(RESEQ_BUILD_PYTHON "Build Python bindings (requires SWIG 3+ and python3-dev)" OFF)
if(RESEQ_BUILD_PYTHON)
  add_subdirectory(python)
endif()
