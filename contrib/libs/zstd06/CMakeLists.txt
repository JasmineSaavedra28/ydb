
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(contrib-libs-zstd06)
target_compile_options(contrib-libs-zstd06 PRIVATE
  -DZSTD_LEGACY_SUPPORT=1
)
target_include_directories(contrib-libs-zstd06 PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/common
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/compress
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/decompress
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/dictBuilder
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy
)
target_link_libraries(contrib-libs-zstd06 PUBLIC
  contrib-libs-cxxsupp
)
target_sources(contrib-libs-zstd06 PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/common/entropy_common.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/common/fse_decompress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/common/xxhash.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/common/zstd_common.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/compress/fse_compress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/compress/huf_compress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/compress/zbuff_compress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/compress/zstd_compress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/decompress/huf_decompress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/decompress/zbuff_decompress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/decompress/zstd_decompress.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/dictBuilder/divsufsort.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/dictBuilder/zdict.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v01.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v02.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v03.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v04.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v05.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v07.c
  ${CMAKE_SOURCE_DIR}/contrib/libs/zstd06/legacy/zstd_v08.c
)
