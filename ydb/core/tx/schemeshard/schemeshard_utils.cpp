#include "schemeshard_utils.h"

#include <ydb/core/mind/hive/hive.h>
#include <ydb/core/protos/counters_schemeshard.pb.h>

namespace NKikimr {
namespace NSchemeShard {

void TShardDeleter::Shutdown(const NActors::TActorContext &ctx) {
    for (auto& info : PerHiveDeletions) {
        NTabletPipe::CloseClient(ctx, info.second.PipeToHive);
    }
    PerHiveDeletions.clear();
}

void TShardDeleter::SendDeleteRequests(TTabletId hiveTabletId,
                                       const THashSet<TShardIdx> &shardsToDelete,
                                       const THashMap<NKikimr::NSchemeShard::TShardIdx, NKikimr::NSchemeShard::TShardInfo>& shardsInfos,
                                       const NActors::TActorContext &ctx) {
    if (shardsToDelete.empty())
        return;

    TPerHiveDeletions& info = PerHiveDeletions[hiveTabletId];
    if (!info.PipeToHive) {
        NTabletPipe::TClientConfig clientConfig;
        clientConfig.RetryPolicy = HivePipeRetryPolicy;
        info.PipeToHive = ctx.Register(NTabletPipe::CreateClient(ctx.SelfID, ui64(hiveTabletId), clientConfig));
    }
    info.ShardsToDelete.insert(shardsToDelete.begin(), shardsToDelete.end());

    for (auto shardIdx : shardsToDelete) {
        ShardHive[shardIdx] = hiveTabletId;
        // !HACK: use shardIdx as  TxId because Hive only replies with TxId
        // TODO: change hive events to get rid of this hack
        // svc@ in progress fixing it
        TAutoPtr<TEvHive::TEvDeleteTablet> event = new TEvHive::TEvDeleteTablet(shardIdx.GetOwnerId(), ui64(shardIdx.GetLocalId()), ui64(shardIdx.GetLocalId()));
        auto itShard = shardsInfos.find(shardIdx);
        if (itShard != shardsInfos.end()) {
            TTabletId shardTabletId = itShard->second.TabletID;
            if (shardTabletId) {
                event->Record.AddTabletID(ui64(shardTabletId));
            }
        }

        Y_VERIFY(shardIdx);

        LOG_DEBUG_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                    "Free shard " << shardIdx << " hive " << hiveTabletId << " at ss " << MyTabletID);

        NTabletPipe::SendData(ctx, info.PipeToHive, event.Release());
    }
}

void TShardDeleter::ResendDeleteRequests(TTabletId hiveTabletId, const THashMap<TShardIdx, TShardInfo>& shardsInfos, const NActors::TActorContext &ctx) {
    LOG_NOTICE_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                 "Resending tablet deletion requests from " << MyTabletID << " to " << hiveTabletId);

    auto itPerHive = PerHiveDeletions.find(hiveTabletId);
    if (itPerHive == PerHiveDeletions.end()) {
        LOG_WARN_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                   "Hive " << hiveTabletId << " not found for delete requests");
        return;
    }

    THashSet<TShardIdx> toResend(std::move(itPerHive->second.ShardsToDelete));
    PerHiveDeletions.erase(itPerHive);

    SendDeleteRequests(hiveTabletId, toResend, shardsInfos, ctx);
}

void TShardDeleter::ResendDeleteRequest(TTabletId hiveTabletId,
                                        const THashMap<TShardIdx, TShardInfo>& shardsInfos,
                                        TShardIdx shardIdx,
                                        const NActors::TActorContext &ctx) {
    LOG_NOTICE_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                 "Resending tablet deletion request from " << MyTabletID << " to " << hiveTabletId);

    auto itPerHive = PerHiveDeletions.find(hiveTabletId);
    if (itPerHive == PerHiveDeletions.end())
        return;

    auto itShardIdx = itPerHive->second.ShardsToDelete.find(shardIdx);
    if (itShardIdx != itPerHive->second.ShardsToDelete.end()) {
        THashSet<TShardIdx> toResend({shardIdx});
        itPerHive->second.ShardsToDelete.erase(itShardIdx);
        if (itPerHive->second.ShardsToDelete.empty()) {
            PerHiveDeletions.erase(itPerHive);
        }
        SendDeleteRequests(hiveTabletId, toResend, shardsInfos, ctx);
    } else {
        LOG_WARN_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                   "Shard " << shardIdx << " not found for delete request for Hive " << hiveTabletId);
    }
}

void TShardDeleter::RedirectDeleteRequest(TTabletId hiveFromTabletId,
                                          TTabletId hiveToTabletId,
                                          TShardIdx shardIdx,
                                          const THashMap<TShardIdx, TShardInfo>& shardsInfos,
                                          const NActors::TActorContext &ctx) {
    LOG_NOTICE_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                 "Redirecting tablet deletion requests from " << hiveFromTabletId << " to " << hiveToTabletId);
    auto itFromHive = PerHiveDeletions.find(hiveFromTabletId);
    if (itFromHive != PerHiveDeletions.end()) {
        auto& toHive(PerHiveDeletions[hiveToTabletId]);
        auto itShardIdx = itFromHive->second.ShardsToDelete.find(shardIdx);
        if (itShardIdx != itFromHive->second.ShardsToDelete.end()) {
            toHive.ShardsToDelete.emplace(*itShardIdx);
            itFromHive->second.ShardsToDelete.erase(itShardIdx);
        } else {
            LOG_WARN_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                       "Shard " << shardIdx << " not found for delete request for Hive " << hiveFromTabletId);
        }
        if (itFromHive->second.ShardsToDelete.empty()) {
            PerHiveDeletions.erase(itFromHive);
        }
    }

    ResendDeleteRequest(hiveToTabletId, shardsInfos, shardIdx, ctx);
}

void TShardDeleter::ShardDeleted(TShardIdx shardIdx, const NActors::TActorContext &ctx) {
    if (!ShardHive.contains(shardIdx))
        return;

    TTabletId hiveTabletId = ShardHive[shardIdx];
    ShardHive.erase(shardIdx);
    PerHiveDeletions[hiveTabletId].ShardsToDelete.erase(shardIdx);

    if (PerHiveDeletions[hiveTabletId].ShardsToDelete.empty()) {
        NTabletPipe::CloseClient(ctx, PerHiveDeletions[hiveTabletId].PipeToHive);
        PerHiveDeletions.erase(hiveTabletId);
    }
}

bool TShardDeleter::Has(TTabletId hiveTabletId, TActorId pipeClientActorId) const {
    return PerHiveDeletions.contains(hiveTabletId) && PerHiveDeletions.at(hiveTabletId).PipeToHive == pipeClientActorId;
}

bool TShardDeleter::Has(TShardIdx shardIdx) const {
    return ShardHive.contains(shardIdx);
}

bool TShardDeleter::Empty() const {
    return PerHiveDeletions.empty();
}

void TSelfPinger::Handle(TEvSchemeShard::TEvMeasureSelfResponseTime::TPtr &ev, const NActors::TActorContext &ctx) {
    Y_UNUSED(ev);
    TInstant now = AppData(ctx)->TimeProvider->Now();
    TDuration responseTime = now - SelfPingSentTime;
    LastResponseTime = responseTime;
    TabletCounters->Simple()[COUNTER_RESPONSE_TIME_USEC].Set(LastResponseTime.MicroSeconds());
    if (responseTime.MilliSeconds() > 1000) {
        LOG_WARN_S(ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                   "Schemeshard " << TabletId << " response time is " << responseTime.MilliSeconds() << " msec");
    }
    SelfPingInFlight = false;
    if (responseTime > SELF_PING_INTERVAL) {
        DoSelfPing(ctx);
    } else {
        SheduleSelfPingWakeup(ctx);
    }
}

void TSelfPinger::Handle(TEvSchemeShard::TEvWakeupToMeasureSelfResponseTime::TPtr &ev, const NActors::TActorContext &ctx) {
    Y_UNUSED(ev);
    SelfPingWakeupScheduled = false;
    DoSelfPing(ctx);
}

void TSelfPinger::OnAnyEvent(const NActors::TActorContext &ctx) {
    TInstant now = AppData(ctx)->TimeProvider->Now();
    if (SelfPingInFlight) {
        TDuration responseTime = now - SelfPingSentTime;
        // Increase measured response time is ping is taking longer than then the previous one
        LastResponseTime = Max(LastResponseTime, responseTime);
        TabletCounters->Simple()[COUNTER_RESPONSE_TIME_USEC].Set(LastResponseTime.MicroSeconds());
    } else if ((now - SelfPingWakeupScheduledTime) > SELF_PING_INTERVAL) {
        DoSelfPing(ctx);
    }
}

void TSelfPinger::DoSelfPing(const NActors::TActorContext &ctx) {
    if (SelfPingInFlight)
        return;

    ctx.Send(ctx.SelfID, new TEvSchemeShard::TEvMeasureSelfResponseTime);
    SelfPingSentTime = AppData(ctx)->TimeProvider->Now();
    SelfPingInFlight = true;
}

void TSelfPinger::SheduleSelfPingWakeup(const NActors::TActorContext &ctx) {
    if (SelfPingWakeupScheduled)
        return;

    ctx.Schedule(SELF_PING_INTERVAL, new TEvSchemeShard::TEvWakeupToMeasureSelfResponseTime);
    SelfPingWakeupScheduled = true;
    SelfPingWakeupScheduledTime = AppData(ctx)->TimeProvider->Now();
}

}

namespace NTableIndex {

TTableColumns ExtractInfo(const NKikimrSchemeOp::TTableDescription &tableDesrc) {
    NTableIndex::TTableColumns result;
    for (auto& column: tableDesrc.GetColumns()) {
        result.Columns.insert(column.GetName());
    }
    for (auto& keyName: tableDesrc.GetKeyColumnNames()) {
        result.Keys.push_back(keyName);
    }
    return result;
}

TIndexColumns ExtractInfo(const NKikimrSchemeOp::TIndexCreationConfig &indexDesc) {
    NTableIndex::TIndexColumns result;
    for (auto& keyName: indexDesc.GetKeyColumnNames()) {
        result.KeyColumns.push_back(keyName);
    }
    for (auto& keyName: indexDesc.GetDataColumnNames()) {
        result.DataColumns.push_back(keyName);
    }
    return result;
}

TTableColumns ExtractInfo(const NSchemeShard::TTableInfo::TPtr &tableInfo) {
    NTableIndex::TTableColumns result;
    for (auto& item: tableInfo->Columns) {
        const auto& column = item.second;
        if (column.IsDropped()) {
            continue;
        }

        result.Columns.insert(item.second.Name);
    }

    for (auto& keyId: tableInfo->KeyColumnIds) {
        const auto& keyColumn = tableInfo->Columns.at(keyId);
        if (keyColumn.IsDropped()) {
            continue;
        }

        Y_VERIFY(result.Columns.contains(keyColumn.Name));
        result.Keys.push_back(keyColumn.Name);
    }

    return result;
}

NKikimrSchemeOp::TTableDescription CalcImplTableDesc(
    const NSchemeShard::TTableInfo::TPtr& baseTableInfo,
    const NTableIndex::TTableColumns& implTableColumns,
    const NKikimrSchemeOp::TTableDescription& indexTableDesc)
{
    NKikimrSchemeOp::TTableDescription result;

    result.SetName("indexImplTable");

    if (indexTableDesc.HasUniformPartitionsCount()) {
        result.SetUniformPartitionsCount(indexTableDesc.GetUniformPartitionsCount());
    }

    if (indexTableDesc.SplitBoundarySize()) {
        result.MutableSplitBoundary()->CopyFrom(indexTableDesc.GetSplitBoundary());
    }

    *result.MutablePartitionConfig() = PartitionConfigForIndexes(baseTableInfo, indexTableDesc);

    //Columns and KeyColumnNames order is really important
    //the order of implTableColumns.Keys is the right one

    THashMap<TString, ui32> implKeyToImplColumn;
    for (ui32 keyId = 0; keyId < implTableColumns.Keys.size(); ++keyId) {
        implKeyToImplColumn[implTableColumns.Keys[keyId]] = keyId;
    }

    const TAppData* appData = AppData();

    result.ClearColumns();
    for (auto& iter: baseTableInfo->Columns) {
        const NSchemeShard::TTableInfo::TColumn& column = iter.second;
        if (column.IsDropped()) {
            continue;
        }

        if (implTableColumns.Columns.contains(column.Name)) {
            auto item = result.AddColumns();
            item->SetName(column.Name);

            item->SetType(appData->TypeRegistry->GetTypeName(column.PType));

            ui32 order = Max<ui32>();
            if (implKeyToImplColumn.contains(column.Name)) {
                order = implKeyToImplColumn.at(column.Name);
            }
            item->SetId(order);
        }
    }

    std::sort(result.MutableColumns()->begin(),
              result.MutableColumns()->end(),
              [] (auto& left, auto& right) {
                  return left.GetId() < right.GetId();
              });

    for (auto& column: *result.MutableColumns()) {
        column.ClearId();
    }

    result.ClearKeyColumnNames();
    for (auto& keyName: implTableColumns.Keys) {
        result.AddKeyColumnNames(keyName);
    }

    return result;
}

NKikimrSchemeOp::TTableDescription CalcImplTableDesc(
    const NKikimrSchemeOp::TTableDescription &baseTableDesrc,
    const TTableColumns &implTableColumns,
    const NKikimrSchemeOp::TTableDescription &indexTableDesc)
{
    NKikimrSchemeOp::TTableDescription result;

    result.SetName("indexImplTable");

    if (indexTableDesc.HasUniformPartitionsCount()) {
        result.SetUniformPartitionsCount(indexTableDesc.GetUniformPartitionsCount());
    }

    if (indexTableDesc.SplitBoundarySize()) {
        result.MutableSplitBoundary()->CopyFrom(indexTableDesc.GetSplitBoundary());
    }

    *result.MutablePartitionConfig() = PartitionConfigForIndexes(baseTableDesrc, indexTableDesc);

    //Columns and KeyColumnNames order is really important
    //the order of implTableColumns.Keys is the right one

    THashMap<TString, ui32> implKeyToImplColumn;
    for (ui32 keyId = 0; keyId < implTableColumns.Keys.size(); ++keyId) {
        implKeyToImplColumn[implTableColumns.Keys[keyId]] = keyId;
    }

    result.ClearColumns();
    for (auto& column: baseTableDesrc.GetColumns()) {
        auto& columnName = column.GetName();
        if (implTableColumns.Columns.contains(columnName)) {
            auto item = result.AddColumns();
            item->CopyFrom(column);

            // Indexes don't use column families
            item->ClearFamily();
            item->ClearFamilyName();

            ui32 order = Max<ui32>();
            if (implKeyToImplColumn.contains(columnName)) {
                order = implKeyToImplColumn.at(columnName);
            }
            item->SetId(order);
        }
    }

    std::sort(result.MutableColumns()->begin(),
              result.MutableColumns()->end(),
              [] (auto& left, auto& right) {
                  return left.GetId() < right.GetId();
              });

    for (auto& column: *result.MutableColumns()) {
        column.ClearId();
    }

    result.ClearKeyColumnNames();
    for (auto& keyName: implTableColumns.Keys) {
        result.AddKeyColumnNames(keyName);
    }

    return result;
}

NKikimrSchemeOp::TPartitionConfig PartitionConfigForIndexes(
        const NKikimrSchemeOp::TPartitionConfig& baseTablePartitionConfig,
        const NKikimrSchemeOp::TTableDescription& indexTableDesc)
{
    // KIKIMR-6687
    NKikimrSchemeOp::TPartitionConfig result;

    if (baseTablePartitionConfig.HasNamedCompactionPolicy()) {
        result.SetNamedCompactionPolicy(baseTablePartitionConfig.GetNamedCompactionPolicy());
    }
    if (baseTablePartitionConfig.HasCompactionPolicy()) {
        result.MutableCompactionPolicy()->CopyFrom(baseTablePartitionConfig.GetCompactionPolicy());
    }
    // skip optional uint64 FollowerCount = 3;
    if (baseTablePartitionConfig.HasExecutorCacheSize()) {
        result.SetExecutorCacheSize(baseTablePartitionConfig.GetExecutorCacheSize());
    }
    // skip     optional bool AllowFollowerPromotion = 5 [default = true];
    if (baseTablePartitionConfig.HasTxReadSizeLimit()) {
        result.SetTxReadSizeLimit(baseTablePartitionConfig.GetTxReadSizeLimit());
    }
    // skip optional uint32 CrossDataCenterFollowerCount = 8;
    if (baseTablePartitionConfig.HasChannelProfileId()) {
        result.SetChannelProfileId(baseTablePartitionConfig.GetChannelProfileId());
    }

    if (indexTableDesc.GetPartitionConfig().HasPartitioningPolicy()) {
        result.MutablePartitioningPolicy()->CopyFrom(indexTableDesc.GetPartitionConfig().GetPartitioningPolicy());
    } else {
        result.MutablePartitioningPolicy()->SetSizeToSplit((ui64)1 << 27);
        //result.MutablePartitioningPolicy()->SetMinPartitionsCount(1); do not auto merge
        result.MutablePartitioningPolicy()->SetMaxPartitionsCount(100);
    }
    if (baseTablePartitionConfig.HasPipelineConfig()) {
        result.MutablePipelineConfig()->CopyFrom(baseTablePartitionConfig.GetPipelineConfig());
    }
    if (baseTablePartitionConfig.ColumnFamiliesSize()) {
        // Indexes don't need column families unless it's the default column family
        for (const auto& family : baseTablePartitionConfig.GetColumnFamilies()) {
            const bool isDefaultFamily = (
                (!family.HasId() && !family.HasName()) ||
                (family.HasId() && family.GetId() == 0) ||
                (family.HasName() && family.GetName() == "default"));
            if (isDefaultFamily) {
                result.AddColumnFamilies()->CopyFrom(family);
            }
        }
    }
    if (baseTablePartitionConfig.HasResourceProfile()) {
        result.SetResourceProfile(baseTablePartitionConfig.GetResourceProfile());
    }
    if (baseTablePartitionConfig.HasDisableStatisticsCalculation()) {
        result.SetDisableStatisticsCalculation(baseTablePartitionConfig.GetDisableStatisticsCalculation());
    }
    if (baseTablePartitionConfig.HasEnableFilterByKey()) {
        result.SetEnableFilterByKey(baseTablePartitionConfig.GetEnableFilterByKey());
    }
    if (baseTablePartitionConfig.HasExecutorFastLogPolicy()) {
        result.SetExecutorFastLogPolicy(baseTablePartitionConfig.GetExecutorFastLogPolicy());
    }
    if (baseTablePartitionConfig.HasEnableEraseCache()) {
        result.SetEnableEraseCache(baseTablePartitionConfig.GetEnableEraseCache());
    }
    if (baseTablePartitionConfig.HasEraseCacheMinRows()) {
        result.SetEraseCacheMinRows(baseTablePartitionConfig.GetEraseCacheMinRows());
    }
    if (baseTablePartitionConfig.HasEraseCacheMaxBytes()) {
        result.SetEraseCacheMaxBytes(baseTablePartitionConfig.GetEraseCacheMaxBytes());
    }
    if (baseTablePartitionConfig.HasKeepSnapshotTimeout()) {
        result.SetKeepSnapshotTimeout(baseTablePartitionConfig.GetKeepSnapshotTimeout());
    }
    // skip repeated NKikimrStorageSettings.TStorageRoom StorageRooms = 17;
    // skip optional NKikimrHive.TFollowerGroup FollowerGroup = 23;

    return result;
}

NKikimrSchemeOp::TPartitionConfig PartitionConfigForIndexes(
    const NSchemeShard::TTableInfo::TPtr& baseTableInfo,
    const NKikimrSchemeOp::TTableDescription& indexTableDesc)
{
    return PartitionConfigForIndexes(baseTableInfo->PartitionConfig(), indexTableDesc);
}

NKikimrSchemeOp::TPartitionConfig PartitionConfigForIndexes(
    const NKikimrSchemeOp::TTableDescription& baseTableDesrc,
    const NKikimrSchemeOp::TTableDescription& indexTableDesc)
{
    return PartitionConfigForIndexes(baseTableDesrc.GetPartitionConfig(), indexTableDesc);
}

bool ExtractTypes(const NKikimrSchemeOp::TTableDescription& baseTableDesrc, TColumnTypes& columsTypes, TString& explain) {
    const NScheme::TTypeRegistry* typeRegistry = AppData()->TypeRegistry;
    Y_VERIFY(typeRegistry);

    for (auto& column: baseTableDesrc.GetColumns()) {
        auto& columnName = column.GetName();
        auto typeName = NMiniKQL::AdaptLegacyYqlType(column.GetType());
        const NScheme::IType* type = typeRegistry->GetType(typeName);
        if (!type) {
            explain += TStringBuilder() << "Type '" << column.GetType() << "' specified for column '" << columnName << "' is not supported by storage";
            return false;
        }
        auto typeId = type->GetTypeId();
        columsTypes[columnName] = typeId;
    }

    return true;
}

TColumnTypes ExtractTypes(const NSchemeShard::TTableInfo::TPtr& baseTableInfo) {
    TColumnTypes columsTypes;
    for (auto& column: baseTableInfo->Columns) {
        auto& columnName = column.second.Name;
        columsTypes[columnName] = column.second.PType;
    }

    return columsTypes;
}

bool IsCompatibleKeyTypes(
    const TColumnTypes& baseTableColumsTypes,
    const TTableColumns& implTableColumns,
    bool uniformTable,
    TString& explain)
{
    const NScheme::TTypeRegistry* typeRegistry = AppData()->TypeRegistry;
    Y_VERIFY(typeRegistry);

    for (const auto& item: baseTableColumsTypes) {
        auto& columnName = item.first;
        auto& typeId = item.second;

        auto typeSP = typeRegistry->GetType(typeId);
        if (!typeSP) {
            explain += TStringBuilder() << "unknown typeId '" << typeId << "' for column '" << columnName << "'";
            return false;
        }

        if (!NScheme::NTypeIds::IsYqlType(typeId)) {
            explain += TStringBuilder() << "Type '" << typeId << "' specified for column '" << columnName << "' is no longer supported";
            return false;
        }
    }


    for (auto& keyName: implTableColumns.Keys) {
        Y_VERIFY(baseTableColumsTypes.contains(keyName));
        auto typeId = baseTableColumsTypes.at(keyName);

        if (uniformTable) {
            switch (typeId) {
            case NScheme::NTypeIds::Uint32:
            case NScheme::NTypeIds::Uint64:
                break;
            default:
                explain += TStringBuilder() << "Column '" << keyName << "' has wrong key type "
                                            << NScheme::GetTypeName(typeId) << " for being key of table with uniform partitioning";
                return false;
            }
        }

        if (!NSchemeShard::IsAllowedKeyType(typeId)) {
            explain += TStringBuilder() << "Column '" << keyName << "' has wrong key type " << NScheme::GetTypeName(typeId) << " for being key";
            return false;
        }
    }

    return true;
}

}

}
