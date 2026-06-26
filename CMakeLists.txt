# bytewright - dependency-free C++17 decoder toolkit.
#
# This CMake build is for local development, the unit tests, and the CLI tools.
# The ClusterFuzzLite fuzzing build is driven separately by
# .clusterfuzzlite/build.sh, which compiles the same sources with the
# sanitizer/fuzzer flags the fuzzing environment provides.
cmake_minimum_required(VERSION 3.13)

project(bytewright
        VERSION 0.4.0
        DESCRIPTION "A toolkit of binary and text format decoders"
        LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BW_BUILD_TESTS "Build the unit tests" ON)
option(BW_BUILD_TOOLS "Build the command-line tools" ON)
option(BW_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# Shared, deterministic warning set. Applied to first-party targets only.
if(MSVC)
  set(BW_WARN_FLAGS /W4 /permissive-)
  if(BW_WARNINGS_AS_ERRORS)
    list(APPEND BW_WARN_FLAGS /WX)
  endif()
else()
  set(BW_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  if(BW_WARNINGS_AS_ERRORS)
    list(APPEND BW_WARN_FLAGS -Werror)
  endif()
endif()

# Helper: declare a first-party static library whose sources live under
# src/<name>/. Globbing keeps the build resilient as files are added; the
# resulting source list is sorted, so output is deterministic.
function(bw_add_library name)
  file(GLOB ${name}_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/*.cpp")
  list(SORT ${name}_SOURCES)
  add_library(bw_${name} STATIC ${${name}_SOURCES})
  target_include_directories(bw_${name}
                             PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_options(bw_${name} PRIVATE ${BW_WARN_FLAGS})
  set_target_properties(bw_${name} PROPERTIES OUTPUT_NAME ${name})
endfunction()

bw_add_library(common)

bw_add_library(binpack)
target_link_libraries(bw_binpack PUBLIC bw_common)

bw_add_library(minidb)
target_link_libraries(bw_minidb PUBLIC bw_common)

bw_add_library(cfgscript)
target_link_libraries(bw_cfgscript PUBLIC bw_common)

bw_add_library(streamcodec)
target_link_libraries(bw_streamcodec PUBLIC bw_common)

if(BW_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(BW_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
