
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(pq-gateway-native)
target_link_libraries(pq-gateway-native PUBLIC
  contrib-libs-cxxsupp
  yutil
  common-token_accessor-client
  library-yql-utils
  cpp-client-ydb_driver
  cpp-client-ydb_persqueue_core
  providers-common-metrics
  providers-common-proto
  pq-cm_client-interface
  providers-pq-provider
)
target_sources(pq-gateway-native PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/providers/pq/gateway/native/yql_pq_gateway.cpp
  ${CMAKE_SOURCE_DIR}/ydb/library/yql/providers/pq/gateway/native/yql_pq_session.cpp
)
