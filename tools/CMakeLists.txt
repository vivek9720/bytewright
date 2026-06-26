# Command-line front-end that links every decoder library.
add_executable(bwdump bwdump.cpp)
target_link_libraries(bwdump
                      PRIVATE bw_binpack bw_minidb bw_cfgscript bw_streamcodec
                              bw_common)
target_compile_options(bwdump PRIVATE ${BW_WARN_FLAGS})
