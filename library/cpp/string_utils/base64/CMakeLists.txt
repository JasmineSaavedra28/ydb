
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-string_utils-base64)
target_link_libraries(cpp-string_utils-base64 PUBLIC
  contrib-libs-cxxsupp
  yutil
  libs-base64-avx2
  libs-base64-ssse3
  libs-base64-neon32
  libs-base64-neon64
  libs-base64-plain32
  libs-base64-plain64
)
target_sources(cpp-string_utils-base64 PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/string_utils/base64/base64.cpp
)
