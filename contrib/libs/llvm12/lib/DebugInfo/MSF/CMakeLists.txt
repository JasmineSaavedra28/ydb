
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(lib-DebugInfo-MSF)
target_include_directories(lib-DebugInfo-MSF PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/DebugInfo/MSF
)
target_link_libraries(lib-DebugInfo-MSF PUBLIC
  contrib-libs-cxxsupp
  contrib-libs-llvm12
  llvm12-lib-Support
)
target_sources(lib-DebugInfo-MSF PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/DebugInfo/MSF/MSFBuilder.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/DebugInfo/MSF/MSFCommon.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/DebugInfo/MSF/MSFError.cpp
  ${CMAKE_SOURCE_DIR}/contrib/libs/llvm12/lib/DebugInfo/MSF/MappedBlockStream.cpp
)
