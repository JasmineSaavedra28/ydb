
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-testing-benchmark)
target_link_libraries(cpp-testing-benchmark PUBLIC
  contrib-libs-cxxsupp
  yutil
  contrib-libs-re2
  library-cpp-colorizer
  cpp-getopt-small
  library-cpp-json
  library-cpp-linear_regression
  cpp-threading-poor_man_openmp
)
target_sources(cpp-testing-benchmark PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/benchmark/bench.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/testing/benchmark/dummy.cpp
)
