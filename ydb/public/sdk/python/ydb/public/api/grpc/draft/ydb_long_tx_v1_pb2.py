# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: ydb/public/api/grpc/draft/ydb_long_tx_v1.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()


from ydb.public.api.protos.draft import ydb_long_tx_pb2 as ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2


DESCRIPTOR = _descriptor.FileDescriptor(
  name='ydb/public/api/grpc/draft/ydb_long_tx_v1.proto',
  package='Ydb.LongTx.V1',
  syntax='proto3',
  serialized_options=b'\n\031com.yandex.ydb.long_tx.v1',
  create_key=_descriptor._internal_create_key,
  serialized_pb=b'\n.ydb/public/api/grpc/draft/ydb_long_tx_v1.proto\x12\rYdb.LongTx.V1\x1a-ydb/public/api/protos/draft/ydb_long_tx.proto2\x96\x03\n\rLongTxService\x12T\n\x07\x42\x65ginTx\x12#.Ydb.LongTx.BeginTransactionRequest\x1a$.Ydb.LongTx.BeginTransactionResponse\x12W\n\x08\x43ommitTx\x12$.Ydb.LongTx.CommitTransactionRequest\x1a%.Ydb.LongTx.CommitTransactionResponse\x12]\n\nRollbackTx\x12&.Ydb.LongTx.RollbackTransactionRequest\x1a\'.Ydb.LongTx.RollbackTransactionResponse\x12<\n\x05Write\x12\x18.Ydb.LongTx.WriteRequest\x1a\x19.Ydb.LongTx.WriteResponse\x12\x39\n\x04Read\x12\x17.Ydb.LongTx.ReadRequest\x1a\x18.Ydb.LongTx.ReadResponseB\x1b\n\x19\x63om.yandex.ydb.long_tx.v1b\x06proto3'
  ,
  dependencies=[ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2.DESCRIPTOR,])



_sym_db.RegisterFileDescriptor(DESCRIPTOR)


DESCRIPTOR._options = None

_LONGTXSERVICE = _descriptor.ServiceDescriptor(
  name='LongTxService',
  full_name='Ydb.LongTx.V1.LongTxService',
  file=DESCRIPTOR,
  index=0,
  serialized_options=None,
  create_key=_descriptor._internal_create_key,
  serialized_start=113,
  serialized_end=519,
  methods=[
  _descriptor.MethodDescriptor(
    name='BeginTx',
    full_name='Ydb.LongTx.V1.LongTxService.BeginTx',
    index=0,
    containing_service=None,
    input_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._BEGINTRANSACTIONREQUEST,
    output_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._BEGINTRANSACTIONRESPONSE,
    serialized_options=None,
    create_key=_descriptor._internal_create_key,
  ),
  _descriptor.MethodDescriptor(
    name='CommitTx',
    full_name='Ydb.LongTx.V1.LongTxService.CommitTx',
    index=1,
    containing_service=None,
    input_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._COMMITTRANSACTIONREQUEST,
    output_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._COMMITTRANSACTIONRESPONSE,
    serialized_options=None,
    create_key=_descriptor._internal_create_key,
  ),
  _descriptor.MethodDescriptor(
    name='RollbackTx',
    full_name='Ydb.LongTx.V1.LongTxService.RollbackTx',
    index=2,
    containing_service=None,
    input_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._ROLLBACKTRANSACTIONREQUEST,
    output_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._ROLLBACKTRANSACTIONRESPONSE,
    serialized_options=None,
    create_key=_descriptor._internal_create_key,
  ),
  _descriptor.MethodDescriptor(
    name='Write',
    full_name='Ydb.LongTx.V1.LongTxService.Write',
    index=3,
    containing_service=None,
    input_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._WRITEREQUEST,
    output_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._WRITERESPONSE,
    serialized_options=None,
    create_key=_descriptor._internal_create_key,
  ),
  _descriptor.MethodDescriptor(
    name='Read',
    full_name='Ydb.LongTx.V1.LongTxService.Read',
    index=4,
    containing_service=None,
    input_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._READREQUEST,
    output_type=ydb_dot_public_dot_api_dot_protos_dot_draft_dot_ydb__long__tx__pb2._READRESPONSE,
    serialized_options=None,
    create_key=_descriptor._internal_create_key,
  ),
])
_sym_db.RegisterServiceDescriptor(_LONGTXSERVICE)

DESCRIPTOR.services_by_name['LongTxService'] = _LONGTXSERVICE

# @@protoc_insertion_point(module_scope)
