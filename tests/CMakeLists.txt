# One test executable per subproject. Each links the matching library plus the
# shared common library and is registered with CTest.
function(bw_add_test name lib)
  file(GLOB ${name}_TEST_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp")
  list(SORT ${name}_TEST_SOURCES)
  if(NOT ${name}_TEST_SOURCES)
    return()
  endif()
  add_executable(test_${name} ${${name}_TEST_SOURCES})
  target_include_directories(test_${name}
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_${name} PRIVATE ${lib} bw_common)
  target_compile_options(test_${name} PRIVATE ${BW_WARN_FLAGS})
  add_test(NAME ${name} COMMAND test_${name})
endfunction()

bw_add_test(common bw_common)
bw_add_test(binpack bw_binpack)
bw_add_test(minidb bw_minidb)
bw_add_test(cfgscript bw_cfgscript)
bw_add_test(streamcodec bw_streamcodec)
