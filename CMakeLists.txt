cmake_minimum_required(VERSION 3.16)

project(OpenMetaC VERSION 0.1.0 LANGUAGES C)

include(CTest)
include(CheckCSourceCompiles)
include(GNUInstallDirs)

option(OMC_BUILD_STATIC "Build static library" ON)
option(OMC_BUILD_SHARED "Build shared library" OFF)
option(OMC_BUILD_TESTS "Build tests" ON)
option(OMC_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(OMC_USE_ZLIB "Enable zlib-backed deflate payload extraction" ON)

if(NOT OMC_BUILD_STATIC AND NOT OMC_BUILD_SHARED)
  message(FATAL_ERROR "Enable at least one of OMC_BUILD_STATIC or OMC_BUILD_SHARED.")
endif()

check_c_source_compiles("
typedef unsigned long omc_t;
typedef char omc_chk[(sizeof(omc_t) == 8) ? 1 : -1];
int main(void) { return 0; }"
  OMC_HAVE_U64_ULONG)

check_c_source_compiles("
__extension__ typedef unsigned long long omc_t;
typedef char omc_chk[(sizeof(omc_t) == 8) ? 1 : -1];
int main(void) { return 0; }"
  OMC_HAVE_U64_ULL)

check_c_source_compiles("
typedef unsigned __int64 omc_t;
typedef char omc_chk[(sizeof(omc_t) == 8) ? 1 : -1];
int main(void) { return 0; }"
  OMC_HAVE_U64_MSVC_INT64)

if(NOT OMC_HAVE_U64_ULONG
   AND NOT OMC_HAVE_U64_ULL
   AND NOT OMC_HAVE_U64_MSVC_INT64)
  message(FATAL_ERROR
    "OpenMeta-c requires one usable 64-bit integer type for offsets and sizes.")
endif()

set(OMC_HAVE_ZLIB 0)
if(OMC_USE_ZLIB)
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    set(OMC_HAVE_ZLIB 1)
  endif()
endif()

add_subdirectory(src)

if(OMC_BUILD_TESTS)
  add_subdirectory(tests)
endif()
