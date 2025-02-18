#include "datashard_impl.h"

namespace NKikimr {
namespace NDataShard {

class TDataShard::TTxCompactTable : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
private:
    TEvDataShard::TEvCompactTable::TPtr Ev;

public:
    TTxCompactTable(TDataShard* ds, TEvDataShard::TEvCompactTable::TPtr ev)
        : TBase(ds)
        , Ev(ev)
    {}

    TTxType GetTxType() const override { return TXTYPE_COMPACT_TABLE; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        auto& record = Ev->Get()->Record;

        if (!Self->IsStateActive()) {
            LOG_WARN_S(ctx, NKikimrServices::TX_DATASHARD,
                "Background compaction tx at non-ready tablet " << Self->TabletID()
                << " state " << Self->State
                << ", requested from " << Ev->Sender);
            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                record.GetPathId().GetOwnerId(),
                record.GetPathId().GetLocalId(),
                NKikimrTxDataShard::TEvCompactTableResult::FAILED);
            ctx.Send(Ev->Sender, std::move(response));
            return true;
        }

        const auto pathId = PathIdFromPathId(record.GetPathId());

        if (Self->GetPathOwnerId() != pathId.OwnerId) {
            LOG_WARN_S(ctx, NKikimrServices::TX_DATASHARD,
                "Background compaction " << Self->TabletID()
                << " of not owned " << pathId
                << ", self path owner id# " << Self->GetPathOwnerId());
            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                pathId,
                NKikimrTxDataShard::TEvCompactTableResult::FAILED);
            ctx.Send(Ev->Sender, std::move(response));
            return true;
        }

        const auto& tableId = pathId.LocalPathId;
        auto it = Self->TableInfos.find(tableId);
        if (it == Self->TableInfos.end()) {
            LOG_WARN_S(ctx, NKikimrServices::TX_DATASHARD,
                "Background compaction " << Self->TabletID()
                << " of unknown " << pathId
                << ", requested from " << Ev->Sender);
            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                pathId,
                NKikimrTxDataShard::TEvCompactTableResult::FAILED);
            ctx.Send(Ev->Sender, std::move(response));
            return true;
        }
        const TUserTable& tableInfo = *it->second;
        const auto localTid = tableInfo.LocalTid;

        ++tableInfo.Stats.BackgroundCompactionRequests;

        auto stats = txc.DB.GetCompactionStats(localTid);

        bool hasBorrowed = false;
        if (stats.PartOwners.size() > 1) {
            hasBorrowed = true;
        } else if (stats.PartOwners.size() == 1) {
            if (*stats.PartOwners.begin() != Self->TabletID()) {
                hasBorrowed = true;
            }
        }

        if (hasBorrowed && !record.GetCompactBorrowed()) {
            // normally we should not receive requests to compact in this case
            // but in some rare cases like schemeshard restart we can
            LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD,
                "Background compaction of tablet# " << Self->TabletID()
                << " of path# " << pathId
                << ", requested from# " << Ev->Sender
                << " contains borrowed parts, failed");

            Self->IncCounter(COUNTER_TX_BACKGROUND_COMPACTION_FAILED_BORROWED);

            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                pathId,
                NKikimrTxDataShard::TEvCompactTableResult::BORROWED);
            ctx.Send(Ev->Sender, std::move(response));
            return true;
        }

        if (Self->Executor()->HasLoanedParts()) {
            // normally we should not receive requests to compact in this case
            // but in some rare cases like schemeshard restart we can
            LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD,
                "Background compaction of tablet# " << Self->TabletID()
                << " of path# " << pathId
                << ", requested from# " << Ev->Sender
                << " contains loaned parts, failed");

            Self->IncCounter(COUNTER_TX_BACKGROUND_COMPACTION_FAILED_LOANED);

            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                pathId,
                NKikimrTxDataShard::TEvCompactTableResult::LOANED);
            ctx.Send(Ev->Sender, std::move(response));
            return true;
        }

        bool isEmpty = stats.PartCount == 0 && stats.MemDataSize == 0;
        bool isSingleParted = stats.PartCount == 1 && stats.MemDataSize == 0;
        if (isEmpty || isSingleParted && !hasBorrowed && !record.HasCompactSinglePartedShards()) {
            // nothing to compact
            LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD,
                "Background compaction of tablet# " << Self->TabletID()
                << " of path# " << pathId
                << ", requested from# " << Ev->Sender
                << " is not needed");

            Self->IncCounter(COUNTER_TX_BACKGROUND_COMPACTION_NOT_NEEDED);

            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                pathId,
                NKikimrTxDataShard::TEvCompactTableResult::NOT_NEEDED);
            ctx.Send(Ev->Sender, std::move(response));
            return true;
        }

        auto compactionId = Self->Executor()->CompactTable(tableInfo.LocalTid);
        if (compactionId) {
            LOG_INFO_S(ctx, NKikimrServices::TX_DATASHARD,
                "Started background compaction# " << compactionId
                << " of " << Self->TabletID()
                << " tableId# " << tableId
                << " localTid# " << localTid
                << ", requested from " << Ev->Sender
                << ", partsCount# " << stats.PartCount
                << ", memtableSize# " << stats.MemDataSize
                << ", memtableWaste# " << stats.MemDataWaste
                << ", memtableRows# " << stats.MemRowCount);

            Self->IncCounter(COUNTER_TX_BACKGROUND_COMPACTION);
            Self->CompactionWaiters[tableInfo.LocalTid].emplace_back(std::make_tuple(compactionId, pathId, Ev->Sender));
            ++tableInfo.Stats.BackgroundCompactionCount;
        } else {
            // compaction failed, for now we don't care
            Self->IncCounter(COUNTER_TX_BACKGROUND_COMPACTION_FAILED_START);
            auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
                Self->TabletID(),
                pathId,
                NKikimrTxDataShard::TEvCompactTableResult::FAILED);
            ctx.Send(Ev->Sender, std::move(response));
        }

        return true;
    }

    void Complete(const TActorContext& ctx) override {
        Y_UNUSED(ctx);
    }
};

class TDataShard::TTxPersistFullCompactionTs : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
    ui64 TableId;
    TInstant Ts;

public:
    TTxPersistFullCompactionTs(TDataShard* ds, ui64 tableId, TInstant ts)
        : TBase(ds)
        , TableId(tableId)
        , Ts(ts)
    {}

    // note, that type is the same as in TTxCompactTable
    TTxType GetTxType() const override { return TXTYPE_COMPACT_TABLE; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        Y_UNUSED(ctx);
        NIceDb::TNiceDb db(txc.DB);
        Self->PersistUserTableFullCompactionTs(db, TableId, Ts.Seconds());
        return true;
    }

    void Complete(const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD,
            "Updated last full compaction of tablet# "<< Self->TabletID()
            << ", tableId# " << TableId
            << ", last full compaction# " << Ts);
    }
};

void TDataShard::Handle(TEvDataShard::TEvCompactTable::TPtr& ev, const TActorContext& ctx) {
    Executor()->Execute(new TTxCompactTable(this, ev), ctx);
}

void TDataShard::CompactionComplete(ui32 tableId, const TActorContext &ctx) {
    auto finishedInfo = Executor()->GetFinishedCompactionInfo(tableId);

    if (tableId >= Schema::MinLocalTid) {
        for (auto& ti : TableInfos) {
            if (ti.second->LocalTid != tableId && ti.second->ShadowTid != tableId)
                continue;
            if (ti.second->Stats.LastFullCompaction < finishedInfo.FullCompactionTs) {
                IncCounter(COUNTER_FULL_COMPACTION_DONE);
                ti.second->Stats.LastFullCompaction = finishedInfo.FullCompactionTs;
                Executor()->Execute(
                    new TTxPersistFullCompactionTs(
                        this,
                        ti.first,
                        finishedInfo.FullCompactionTs),
                    ctx);
            }

            ti.second->StatsNeedUpdate = true;
            UpdateTableStats(ctx);
            break;
        }
    }

    ReplyCompactionWaiters(tableId, finishedInfo.Edge, ctx);
}

void TDataShard::ReplyCompactionWaiters(ui32 tableId, ui64 edge, const TActorContext &ctx) {
    LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD,
        "CompactionComplete of tablet# "<< TabletID()
        << ", table# " << tableId
        << ", finished edge# " << edge
        << ", front# " << (CompactionWaiters[tableId].empty() ? 0UL : std::get<0>(CompactionWaiters[tableId].front())));

    while (!CompactionWaiters[tableId].empty()) {
        const auto& waiter = CompactionWaiters[tableId].front();
        if (std::get<0>(waiter) > edge)
            break;

        const auto& pathId = std::get<1>(waiter);
        const auto& sender = std::get<2>(waiter);
        auto response = MakeHolder<TEvDataShard::TEvCompactTableResult>(
            TabletID(),
            pathId.OwnerId,
            pathId.LocalPathId,
            NKikimrTxDataShard::TEvCompactTableResult::OK);
        ctx.Send(sender, std::move(response));

        LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD,
            "Sending TEvCompactTableResult to# " << sender
            << "pathId# " << pathId);

        CompactionWaiters[tableId].pop_front();
    }
}

void TDataShard::Handle(TEvDataShard::TEvGetCompactTableStats::TPtr& ev, const TActorContext& ctx) {
    auto &record = ev->Get()->Record;
    auto response = MakeHolder<TEvDataShard::TEvGetCompactTableStatsResult>();

    const auto pathId = PathIdFromPathId(record.GetPathId());

    const auto& tableId = pathId.LocalPathId;
    auto it = TableInfos.find(tableId);
    if (it != TableInfos.end()) {
        const TUserTable& tableInfo = *it->second;
        response->Record.SetBackgroundCompactionRequests(tableInfo.Stats.BackgroundCompactionRequests);
        response->Record.SetBackgroundCompactionCount(tableInfo.Stats.BackgroundCompactionCount);
    }

    ctx.Send(ev->Sender, std::move(response));
}

} // NDataShard
} // NKikimr
