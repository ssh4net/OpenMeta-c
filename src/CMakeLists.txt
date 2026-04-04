file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/omc")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/omc/omc_cfg.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/omc/omc_cfg.h"
  @ONLY
)

function(omc_apply_warnings target_name)
  set_target_properties(${target_name} PROPERTIES
    C_STANDARD 90
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
  )

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
  else()
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wshadow
      -Wconversion
      -Wstrict-prototypes
      -Wmissing-prototypes
      -Werror=return-type
      -pedantic
    )
  endif()

  if(OMC_WARNINGS_AS_ERRORS)
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /WX)
    else()
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()
endfunction()

set(OMC_SOURCES
  base/omc_arena.c
  core/omc_edit.c
  core/omc_key.c
  core/omc_val.c
  core/omc_store.c
  edit/omc_transfer.c
  edit/omc_transfer_persist.c
  edit/omc_xmp_apply.c
  edit/omc_xmp_embed.c
  edit/omc_xmp_dump.c
  edit/omc_xmp_write.c
  read/omc_bmff.c
  read/omc_ciff.c
  read/omc_exr.c
  read/omc_exif.c
  read/omc_exif_name.c
  read/omc_icc.c
  read/omc_iptc.c
  read/omc_irb.c
  read/omc_jumbf.c
  read/omc_pay.c
  read/omc_read.c
  read/omc_scan.c
  read/omc_xmp.c
)

function(omc_add_target target_name target_kind)
  add_library(${target_name} ${target_kind} ${OMC_SOURCES})
  target_include_directories(${target_name} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )
  if(OMC_HAVE_ZLIB)
    if(TARGET ZLIB::ZLIB)
      target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
    else()
      target_include_directories(${target_name} PRIVATE ${ZLIB_INCLUDE_DIRS})
      target_link_libraries(${target_name} PRIVATE ${ZLIB_LIBRARIES})
    endif()
  endif()
  if(OMC_HAVE_BROTLI)
    target_include_directories(${target_name} PRIVATE ${OMC_BROTLI_INCLUDE_DIR})
    target_link_libraries(${target_name} PRIVATE ${OMC_BROTLI_DEC_LIB})
    if(OMC_BROTLI_COMMON_LIB)
      target_link_libraries(${target_name} PRIVATE ${OMC_BROTLI_COMMON_LIB})
    endif()
  endif()
  omc_apply_warnings(${target_name})
  set_target_properties(${target_name} PROPERTIES OUTPUT_NAME omc)
endfunction()

if(OMC_BUILD_STATIC)
  omc_add_target(omc_static STATIC)
endif()

if(OMC_BUILD_SHARED)
  omc_add_target(omc_shared SHARED)
  target_compile_definitions(omc_shared PRIVATE
    OMC_BUILD_SHARED=1
    OMC_BUILDING_SHARED=1
  )
endif()

if(TARGET omc_static)
  add_library(omc ALIAS omc_static)
elseif(TARGET omc_shared)
  add_library(omc ALIAS omc_shared)
endif()
