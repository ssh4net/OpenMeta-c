add_executable(omc_test_store test_omc_store.c)
target_link_libraries(omc_test_store PRIVATE omc)
add_test(NAME omc_test_store COMMAND omc_test_store)

add_executable(omc_test_scan test_omc_scan.c)
target_link_libraries(omc_test_scan PRIVATE omc)
add_test(NAME omc_test_scan COMMAND omc_test_scan)

add_executable(omc_test_exif test_omc_exif.c)
target_link_libraries(omc_test_exif PRIVATE omc)
add_test(NAME omc_test_exif COMMAND omc_test_exif)

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
