
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(library-cpp-ipmath)
target_link_libraries(library-cpp-ipmath PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-ipv6_address
)
target_sources(library-cpp-ipmath PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/ipmath/ipmath.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/ipmath/range_set.cpp
)
