
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-threading-queue)
target_link_libraries(cpp-threading-queue PUBLIC
  contrib-libs-cxxsupp
  yutil
)
target_sources(cpp-threading-queue PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/threading/queue/mpmc_unordered_ring.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/threading/queue/mpsc_htswap.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/threading/queue/mpsc_intrusive_unordered.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/threading/queue/mpsc_read_as_filled.cpp
  ${CMAKE_SOURCE_DIR}/library/cpp/threading/queue/mpsc_vinfarr_obstructive.cpp
)
