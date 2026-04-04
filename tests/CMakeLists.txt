add_executable(omc_test_store test_omc_store.c)
target_link_libraries(omc_test_store PRIVATE omc)
add_test(NAME omc_test_store COMMAND omc_test_store)

add_executable(omc_test_edit test_omc_edit.c)
target_link_libraries(omc_test_edit PRIVATE omc)
add_test(NAME omc_test_edit COMMAND omc_test_edit)

add_executable(omc_test_scan test_omc_scan.c)
target_link_libraries(omc_test_scan PRIVATE omc)
add_test(NAME omc_test_scan COMMAND omc_test_scan)

add_executable(omc_test_exif test_omc_exif.c)
target_link_libraries(omc_test_exif PRIVATE omc)
add_test(NAME omc_test_exif COMMAND omc_test_exif)

add_executable(omc_test_exr test_omc_exr.c)
target_link_libraries(omc_test_exr PRIVATE omc)
add_test(NAME omc_test_exr COMMAND omc_test_exr)

add_executable(omc_test_exif_name test_omc_exif_name.c)
target_link_libraries(omc_test_exif_name PRIVATE omc)
add_test(NAME omc_test_exif_name COMMAND omc_test_exif_name)

add_executable(omc_test_icc test_omc_icc.c)
target_link_libraries(omc_test_icc PRIVATE omc)
add_test(NAME omc_test_icc COMMAND omc_test_icc)

add_executable(omc_test_iptc test_omc_iptc.c)
target_link_libraries(omc_test_iptc PRIVATE omc)
add_test(NAME omc_test_iptc COMMAND omc_test_iptc)

add_executable(omc_test_pay test_omc_pay.c)
target_link_libraries(omc_test_pay PRIVATE omc)
add_test(NAME omc_test_pay COMMAND omc_test_pay)

add_executable(omc_test_read test_omc_read.c)
target_link_libraries(omc_test_read PRIVATE omc)
add_test(NAME omc_test_read COMMAND omc_test_read)

add_executable(omc_test_irb test_omc_irb.c)
target_link_libraries(omc_test_irb PRIVATE omc)
add_test(NAME omc_test_irb COMMAND omc_test_irb)

add_executable(omc_test_jumbf test_omc_jumbf.c)
target_link_libraries(omc_test_jumbf PRIVATE omc)
add_test(NAME omc_test_jumbf COMMAND omc_test_jumbf)

add_executable(omc_test_xmp test_omc_xmp.c)
target_link_libraries(omc_test_xmp PRIVATE omc)
add_test(NAME omc_test_xmp COMMAND omc_test_xmp)

add_executable(omc_test_xmp_dump test_omc_xmp_dump.c)
target_link_libraries(omc_test_xmp_dump PRIVATE omc)
add_test(NAME omc_test_xmp_dump COMMAND omc_test_xmp_dump)

add_executable(omc_test_xmp_embed test_omc_xmp_embed.c)
target_link_libraries(omc_test_xmp_embed PRIVATE omc)
add_test(NAME omc_test_xmp_embed COMMAND omc_test_xmp_embed)

add_executable(omc_test_xmp_apply test_omc_xmp_apply.c)
target_link_libraries(omc_test_xmp_apply PRIVATE omc)
add_test(NAME omc_test_xmp_apply COMMAND omc_test_xmp_apply)

add_executable(omc_test_xmp_write test_omc_xmp_write.c)
target_link_libraries(omc_test_xmp_write PRIVATE omc)
add_test(NAME omc_test_xmp_write COMMAND omc_test_xmp_write)

add_executable(omc_test_transfer test_omc_transfer.c)
target_link_libraries(omc_test_transfer PRIVATE omc)
add_test(NAME omc_test_transfer COMMAND omc_test_transfer)

add_executable(omc_test_transfer_persist test_omc_transfer_persist.c)
target_link_libraries(omc_test_transfer_persist PRIVATE omc)
add_test(NAME omc_test_transfer_persist COMMAND omc_test_transfer_persist)

if(OMC_BUILD_PARITY_TESTS)
  set(omc_openmeta_paths)
  set(omc_openmeta_build_root "")
  set(omc_openmeta_lib "")
  set(omc_openmeta_config "")
  if(OMC_OPENMETA_DIR)
    list(APPEND omc_openmeta_paths "${OMC_OPENMETA_DIR}")
  endif()
  list(APPEND omc_openmeta_paths
       "${CMAKE_CURRENT_SOURCE_DIR}/../../OpenMeta/build"
       "${CMAKE_CURRENT_SOURCE_DIR}/../../OpenMeta/build-tests"
       "${CMAKE_CURRENT_SOURCE_DIR}/../../OpenMeta/build/install/lib/cmake/OpenMeta")

  foreach(omc_openmeta_path IN LISTS omc_openmeta_paths)
    if(EXISTS "${omc_openmeta_path}/libopenmeta.a"
       AND EXISTS
         "${omc_openmeta_path}/generated/include/openmeta/build_info_generated.h") 
      set(omc_openmeta_build_root "${omc_openmeta_path}")
      set(omc_openmeta_lib "${omc_openmeta_path}/libopenmeta.a")
      break()
    endif()
    if(EXISTS "${omc_openmeta_path}/libopenmeta.so"
       AND EXISTS
         "${omc_openmeta_path}/generated/include/openmeta/build_info_generated.h")
      set(omc_openmeta_build_root "${omc_openmeta_path}")
      set(omc_openmeta_lib "${omc_openmeta_path}/libopenmeta.so")
      break()
    endif()
    if(EXISTS "${omc_openmeta_path}/OpenMetaConfig.cmake"
       AND EXISTS "${omc_openmeta_path}/OpenMetaTargets.cmake")
      set(omc_openmeta_config
          "${omc_openmeta_path}/OpenMetaConfig.cmake")
    endif()
  endforeach()

  if(omc_openmeta_build_root)
    set(omc_openmeta_repo_root
        "${CMAKE_CURRENT_SOURCE_DIR}/../../OpenMeta")
    set(omc_openmeta_link_libs)

    if(NOT EXISTS "${omc_openmeta_repo_root}/src/include/openmeta/simple_meta.h")
      message(FATAL_ERROR
        "OpenMeta source headers were not found next to OpenMeta-c.")
    endif()

    list(PREPEND CMAKE_MODULE_PATH "${omc_openmeta_repo_root}/cmake")
    find_package(ZLIB QUIET)
    find_package(Brotli QUIET)
    find_package(EXPAT QUIET)

    add_library(omc_openmeta_upstream UNKNOWN IMPORTED)
    set_target_properties(omc_openmeta_upstream PROPERTIES
      IMPORTED_LOCATION "${omc_openmeta_lib}"
      INTERFACE_INCLUDE_DIRECTORIES
        "${omc_openmeta_repo_root}/src/include;${omc_openmeta_build_root}/generated/include"
    )

    if(ZLIB_FOUND)
      list(APPEND omc_openmeta_link_libs ZLIB::ZLIB)
    endif()
    if(TARGET Brotli::decoder)
      list(APPEND omc_openmeta_link_libs Brotli::decoder)
    endif()
    if(TARGET EXPAT::EXPAT)
      list(APPEND omc_openmeta_link_libs EXPAT::EXPAT)
    endif()
    if(omc_openmeta_link_libs)
      set_target_properties(omc_openmeta_upstream PROPERTIES
        INTERFACE_LINK_LIBRARIES "${omc_openmeta_link_libs}"
      )
    endif()

    add_library(OpenMeta::openmeta ALIAS omc_openmeta_upstream)
  elseif(omc_openmeta_config)
    find_package(EXPAT QUIET)
    include("${omc_openmeta_config}")
  else()
    message(FATAL_ERROR
      "Could not find an OpenMeta build tree or a complete package export. "
      "Set OMC_OPENMETA_DIR to an OpenMeta build directory or package directory.")
  endif()

  add_executable(omc_test_parity test_omc_parity.cc)
  set_target_properties(omc_test_parity PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
  target_link_libraries(omc_test_parity PRIVATE omc OpenMeta::openmeta)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(omc_test_parity PRIVATE -stdlib=libc++)
    target_link_options(omc_test_parity PRIVATE -stdlib=libc++)
  endif()
  add_test(NAME omc_test_parity COMMAND omc_test_parity)
endif()
