
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(validation-ut-protos)
target_link_libraries(validation-ut-protos PUBLIC
  contrib-libs-cxxsupp
  yutil
  api-protos-annotations
  contrib-libs-protobuf
)
target_proto_messages(validation-ut-protos PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/public/lib/validation/ut/protos/validation_test.proto
)
target_proto_addincls(validation-ut-protos
  ./
  ${CMAKE_SOURCE_DIR}/
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/contrib/libs/protobuf/src
)
target_proto_outs(validation-ut-protos
  --cpp_out=${CMAKE_BINARY_DIR}/
  --cpp_styleguide_out=${CMAKE_BINARY_DIR}/
)
target_proto_plugin(validation-ut-protos
  validation
  validation
)
