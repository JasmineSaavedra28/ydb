#pragma once

#include "pq.h"
#include "user_info.h"

#include <ydb/core/testlib/basics/runtime.h>
#include <ydb/core/tablet_flat/tablet_flat_executed.h>
#include <ydb/core/tx/schemeshard/schemeshard.h>
#include <ydb/public/lib/base/msgbus.h>
#include <ydb/core/keyvalue/keyvalue_events.h>
#include <ydb/core/persqueue/events/global.h>
#include <ydb/core/tablet/tablet_counters_aggregator.h>
#include <ydb/core/persqueue/key.h>
#include <ydb/core/keyvalue/keyvalue_events.h>
#include <ydb/core/persqueue/partition.h>
#include <ydb/core/engine/minikql/flat_local_tx_factory.h>
#include <ydb/core/security/ticket_parser.h>

#include <ydb/core/testlib/fake_scheme_shard.h>
#include <ydb/core/testlib/tablet_helpers.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/system/sanitizers.h>
#include <util/system/valgrind.h>

const bool ENABLE_DETAILED_PQ_LOG = false;
const bool ENABLE_DETAILED_KV_LOG = false;

namespace NKikimr {
namespace {

template <typename T>
inline constexpr static T PlainOrSoSlow(T plain, T slow) noexcept {
    return NSan::PlainOrUnderSanitizer(
        NValgrind::PlainOrUnderValgrind(plain, slow),
        slow
    );
}

constexpr ui32 NUM_WRITES = PlainOrSoSlow(100, 1);

void SetupLogging(TTestActorRuntime& runtime) {
    NActors::NLog::EPriority pqPriority = ENABLE_DETAILED_PQ_LOG ? NLog::PRI_TRACE : NLog::PRI_ERROR;
    NActors::NLog::EPriority priority = ENABLE_DETAILED_KV_LOG ? NLog::PRI_DEBUG : NLog::PRI_ERROR;
    NActors::NLog::EPriority otherPriority = NLog::PRI_INFO;

    runtime.SetLogPriority(NKikimrServices::PERSQUEUE, pqPriority);
    runtime.SetLogPriority(NKikimrServices::KEYVALUE, priority);
    runtime.SetLogPriority(NKikimrServices::BOOTSTRAPPER, priority);
    runtime.SetLogPriority(NKikimrServices::TABLET_MAIN, priority);
    runtime.SetLogPriority(NKikimrServices::TABLET_EXECUTOR, priority);
    runtime.SetLogPriority(NKikimrServices::BS_PROXY, priority);

    runtime.SetLogPriority(NKikimrServices::HIVE, otherPriority);
    runtime.SetLogPriority(NKikimrServices::LOCAL, otherPriority);
    runtime.SetLogPriority(NKikimrServices::BS_NODE, otherPriority);
    runtime.SetLogPriority(NKikimrServices::BS_CONTROLLER, otherPriority);
    runtime.SetLogPriority(NKikimrServices::TABLET_RESOLVER, otherPriority);

    runtime.SetLogPriority(NKikimrServices::PIPE_CLIENT, otherPriority);
    runtime.SetLogPriority(NKikimrServices::PIPE_SERVER, otherPriority);

}

class TInitialEventsFilter : TNonCopyable {
    bool IsDone;
public:
    TInitialEventsFilter()
        : IsDone(false)
    {}

    TTestActorRuntime::TEventFilter Prepare() {
        IsDone = false;
        return [&](TTestActorRuntimeBase& runtime, TAutoPtr<IEventHandle>& event) {
            return (*this)(runtime, event);
        };
    }

    bool operator()(TTestActorRuntimeBase& runtime, TAutoPtr<IEventHandle>& event) {
        Y_UNUSED(runtime);
        Y_UNUSED(event);
        return false;
    }
};

} // anonymous namespace


struct TTestContext {
    TTabletTypes::EType TabletType;
    ui64 TabletId;
    ui64 BalancerTabletId;
    TInitialEventsFilter InitialEventsFilter;
    TVector<ui64> TabletIds;
    THolder<TTestActorRuntime> Runtime;
    TActorId Edge;
    THashMap<ui32, ui32> MsgSeqNoMap;


    TTestContext() {
        TabletType = TTabletTypes::PERSQUEUE;
        TabletId = MakeTabletID(0, 0, 1);
        TabletIds.push_back(TabletId);

        BalancerTabletId = MakeTabletID(0, 0, 2);
        TabletIds.push_back(BalancerTabletId);
    }

    static bool RequestTimeoutFilter(TTestActorRuntimeBase& runtime, TAutoPtr<IEventHandle>& event, TDuration duration, TInstant& deadline) {
        if (event->GetTypeRewrite() == TEvents::TSystem::Wakeup) {
            TActorId actorId = event->GetRecipientRewrite();
            IActor *actor = runtime.FindActor(actorId);
            if (actor && actor->GetActivityType() == NKikimrServices::TActivity::PERSQUEUE_ANS_ACTOR) {
                return true;
            }
        }

        Y_UNUSED(deadline);
        Y_UNUSED(duration);

        return false;
    }

    static bool ImmediateLogFlushAndRequestTimeoutFilter(TTestActorRuntimeBase& runtime, TAutoPtr<IEventHandle>& event, TDuration duration, TInstant& deadline) {
        if (event->Type == NKikimr::TEvents::TEvFlushLog::EventType) {
            deadline = TInstant();
            return false;
        }

        deadline = runtime.GetTimeProvider()->Now() + duration;
        return RequestTimeoutFilter(runtime, event, duration, deadline);
    }

    void Prepare(const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& outActiveZone) {
        Y_UNUSED(dispatchName);
        outActiveZone = false;
        Runtime.Reset(new TTestBasicRuntime);
        Runtime->SetScheduledLimit(200);

        SetupLogging(*Runtime);
        SetupTabletServices(*Runtime);
        setup(*Runtime);
        CreateTestBootstrapper(*Runtime,
            CreateTestTabletInfo(TabletId, TabletType, TErasureType::ErasureNone),
            &CreatePersQueue);

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        Runtime->GetAppData(0).PQConfig.SetEnabled(true);

        Runtime->DispatchEvents(options);

        CreateTestBootstrapper(*Runtime,
            CreateTestTabletInfo(BalancerTabletId, TTabletTypes::PERSQUEUE_READ_BALANCER, TErasureType::ErasureNone),
            &CreatePersQueueReadBalancer);

        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        Runtime->DispatchEvents(options);

        Edge = Runtime->AllocateEdgeActor();

        Runtime->SetScheduledEventFilter(&RequestTimeoutFilter);

        outActiveZone = true;
    }

    void Prepare() {
        Runtime.Reset(new TTestBasicRuntime);
        Runtime->SetScheduledLimit(200);
        SetupLogging(*Runtime);
        SetupTabletServices(*Runtime);
        CreateTestBootstrapper(*Runtime,
            CreateTestTabletInfo(TabletId, TabletType, TErasureType::ErasureNone),
            &CreatePersQueue);

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        Runtime->DispatchEvents(options);

        CreateTestBootstrapper(*Runtime,
            CreateTestTabletInfo(BalancerTabletId, TTabletTypes::PERSQUEUE_READ_BALANCER, TErasureType::ErasureNone),
            &CreatePersQueueReadBalancer);

        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        Runtime->DispatchEvents(options);

        Edge = Runtime->AllocateEdgeActor();

        Runtime->SetScheduledEventFilter(&RequestTimeoutFilter);
        Runtime->GetAppData(0).PQConfig.SetEnabled(true);
    }


    void Finalize() {
        Runtime.Reset(nullptr);
    }
};

struct TFinalizer {
    TTestContext& TestContext;

    TFinalizer(TTestContext& testContext)
        : TestContext(testContext)
    {}

    ~TFinalizer() {
        TestContext.Finalize();
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SINGLE COMMAND TEST FUNCTIONS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PQTabletPrepare(ui32 mcip, ui64 msip, ui32 deleteTime, const TVector<std::pair<TString, bool>>& users, TTestContext& tc, int partitions = 2, ui32 lw = 6_MB, bool localDC = true, ui64 ts = 0, ui64 sidMaxCount = 0, ui32 specVersion = 0) {
    TAutoPtr<IEventHandle> handle;
    static int version = 0;
    if (specVersion) {
        version = specVersion;
    } else {
        ++version;
    }
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();

            THolder<TEvPersQueue::TEvUpdateConfig> request(new TEvPersQueue::TEvUpdateConfig());
            for (i32 i = 0; i < partitions; ++i) {
                request->Record.MutableTabletConfig()->AddPartitionIds(i);
            }
            request->Record.MutableTabletConfig()->SetCacheSize(10_MB);
            request->Record.SetTxId(12345);
            auto tabletConfig = request->Record.MutableTabletConfig();
            tabletConfig->SetTopicName("rt3.dc1--topic");
            tabletConfig->SetTopic("topic");
            tabletConfig->SetVersion(version);
            tabletConfig->SetLocalDC(localDC);
            tabletConfig->AddReadRules("user");
            tabletConfig->AddReadFromTimestampsMs(ts);
            auto config = tabletConfig->MutablePartitionConfig();
            config->SetMaxCountInPartition(mcip);
            config->SetMaxSizeInPartition(msip);
            config->SetLifetimeSeconds(deleteTime);
            config->SetSourceIdLifetimeSeconds(1*60*60);
            if (sidMaxCount > 0)
                config->SetSourceIdMaxCounts(sidMaxCount);
            config->SetMaxWriteInflightSize(90000000);
            config->SetLowWatermark(lw);

            for (auto& u : users) {
                if (u.second)
                config->AddImportantClientId(u.first);
                if (u.first != "user")
                    tabletConfig->AddReadRules(u.first);
            }
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            TEvPersQueue::TEvUpdateConfigResponse* result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvUpdateConfigResponse>(handle);

            UNIT_ASSERT(result);
            auto& rec = result->Record;
            UNIT_ASSERT(rec.HasStatus() && rec.GetStatus() == NKikimrPQ::OK);
            UNIT_ASSERT(rec.HasTxId() && rec.GetTxId() == 12345);
            UNIT_ASSERT(rec.HasOrigin() && result->GetOrigin() == 1);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT(retriesLeft >= 1);
        }
    }
    TEvKeyValue::TEvResponse *result;
    THolder<TEvKeyValue::TEvRequest> request;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {

            request.Reset(new TEvKeyValue::TEvRequest);
            auto read = request->Record.AddCmdRead();
            read->SetKey("_config");

            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvKeyValue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetStatus(), NMsgBusProxy::MSTATUS_OK);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT(retriesLeft >= 1);
        }
    }
}



void BalancerPrepare(const TString topic, const TVector<std::pair<ui32, std::pair<ui64, ui32>>>& map, const ui64 ssId, TTestContext& tc, const bool requireAuth = false) {
    TAutoPtr<IEventHandle> handle;
    static int version = 0;
    ++version;

    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();

            THolder<TEvPersQueue::TEvUpdateBalancerConfig> request(new TEvPersQueue::TEvUpdateBalancerConfig());
            for (const auto& p : map) {
                auto part = request->Record.AddPartitions();
                part->SetPartition(p.first);
                part->SetGroup(p.second.second);
                part->SetTabletId(p.second.first);

                auto tablet = request->Record.AddTablets();
                tablet->SetTabletId(p.second.first);
                tablet->SetOwner(1);
                tablet->SetIdx(p.second.first);
            }
            request->Record.SetTxId(12345);
            request->Record.SetPathId(1);
            request->Record.SetVersion(version);
            request->Record.SetTopicName(topic);
            request->Record.SetPath("path");
            request->Record.SetSchemeShardId(ssId);
            request->Record.MutableTabletConfig()->AddReadRules("client");
            request->Record.MutableTabletConfig()->SetRequireAuthWrite(requireAuth);
            request->Record.MutableTabletConfig()->SetRequireAuthRead(requireAuth);

            tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            TEvPersQueue::TEvUpdateConfigResponse* result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvUpdateConfigResponse>(handle);

            UNIT_ASSERT(result);
            auto& rec = result->Record;
            UNIT_ASSERT(rec.HasStatus() && rec.GetStatus() == NKikimrPQ::OK);
            UNIT_ASSERT(rec.HasTxId() && rec.GetTxId() == 12345);
            UNIT_ASSERT(rec.HasOrigin() && result->GetOrigin() == tc.BalancerTabletId);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT(retriesLeft >= 1);
        }
    }
    //TODO: check state
    TTestActorRuntime& runtime = *tc.Runtime;

    ForwardToTablet(runtime, tc.BalancerTabletId, tc.Edge, new TEvents::TEvPoisonPill());
    TDispatchOptions rebootOptions;
    rebootOptions.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvRestored, 2));
    runtime.DispatchEvents(rebootOptions);
}


void PQGetPartInfo(ui64 startOffset, ui64 endOffset, TTestContext& tc) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvOffsetsResponse *result;
    THolder<TEvPersQueue::TEvOffsets> request;

    for (i32 retriesLeft = 3; retriesLeft > 0; --retriesLeft) {
        try {

            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvOffsets);

            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvOffsetsResponse>(handle);
            UNIT_ASSERT(result);

            if (result->Record.PartResultSize() == 0 || result->Record.GetPartResult(0).GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            UNIT_ASSERT(result->Record.PartResultSize());
            UNIT_ASSERT_VALUES_EQUAL((ui64)result->Record.GetPartResult(0).GetStartOffset(), startOffset);
            UNIT_ASSERT_VALUES_EQUAL((ui64)result->Record.GetPartResult(0).GetEndOffset(), endOffset);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT(retriesLeft > 0);
        }
    }

}

void RestartTablet(TTestContext& tc) {
    TTestActorRuntime& runtime = *tc.Runtime;

    ForwardToTablet(runtime, tc.TabletId, tc.Edge, new TEvents::TEvPoisonPill());
    TDispatchOptions rebootOptions;
    rebootOptions.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvRestored, 2));
    runtime.DispatchEvents(rebootOptions);
}


TActorId SetOwner(const ui32 partition, TTestContext& tc, const TString& owner, bool force) {
    TActorId pipeClient = tc.Runtime->ConnectToPipe(tc.TabletId, tc.Edge, 0, GetPipeConfigWithRetries());

    THolder<TEvPersQueue::TEvRequest> request;

    request.Reset(new TEvPersQueue::TEvRequest);
    auto req = request->Record.MutablePartitionRequest();
    req->SetPartition(partition);
    req->MutableCmdGetOwnership()->SetOwner(owner);
    req->MutableCmdGetOwnership()->SetForce(force);
    ActorIdToProto(pipeClient, req->MutablePipeClient());

    tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries(), pipeClient);
    return pipeClient;
}

TActorId RegisterReadSession(const TString& session, TTestContext& tc, const TVector<ui32>& groups = {}) {
    TActorId pipeClient = tc.Runtime->ConnectToPipe(tc.BalancerTabletId, tc.Edge, 0, GetPipeConfigWithRetries());

    THolder<TEvPersQueue::TEvRegisterReadSession> request;

    request.Reset(new TEvPersQueue::TEvRegisterReadSession);
    auto& req = request->Record;
    req.SetSession(session);
    ActorIdToProto(pipeClient, req.MutablePipeClient());
    req.SetClientId("user");
    for (const auto& g : groups) {
        req.AddGroups(g);
    }

    tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries(), pipeClient);
    return pipeClient;
}

void WaitSessionKill(TTestContext& tc) {
    TAutoPtr<IEventHandle> handle;

    tc.Runtime->ResetScheduledCount();

    TEvPersQueue::TEvError *result;
    result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvError>(handle);
    UNIT_ASSERT(result);
    Cerr << "ANS: " << result->Record << "\n";
//    UNIT_ASSERT_EQUAL(result->Record.GetSession(), session);
}


void WaitPartition(const TString &session, TTestContext& tc, ui32 partition, const TString& sessionToRelease, const TString& topic, const TActorId& pipe, bool ok = true) {
    TAutoPtr<IEventHandle> handle;

    tc.Runtime->ResetScheduledCount();

    for (ui32 i = 0; i < 3; ++i) {
        Cerr << "STEP " << i << " ok " << ok << "\n";

        try {
            tc.Runtime->ResetScheduledCount();
            if (i % 2 == 0) {
                TEvPersQueue::TEvLockPartition *result;
                result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvLockPartition>(handle);
                UNIT_ASSERT(result);
                Cerr << "ANS: " << result->Record << "\n";
                UNIT_ASSERT(ok);
                UNIT_ASSERT_EQUAL(result->Record.GetSession(), session);
                break;
            } else {
                TEvPersQueue::TEvReleasePartition *result;
                result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvReleasePartition>(handle);
                UNIT_ASSERT(result);

                Cerr << "ANS2: " << result->Record << "\n";

                UNIT_ASSERT_EQUAL(result->Record.GetSession(), sessionToRelease);
                UNIT_ASSERT(ok);

                THolder<TEvPersQueue::TEvPartitionReleased> request;

                request.Reset(new TEvPersQueue::TEvPartitionReleased);
                auto& req = request->Record;
                req.SetSession(sessionToRelease);
                req.SetPartition(partition);
                req.SetTopic(topic);
                req.SetClientId("user");
                ActorIdToProto(pipe, req.MutablePipeClient());

                tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries(), pipe);
            }
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT(i < 2 || !ok);
        } catch (NActors::TEmptyEventQueueException) {
            UNIT_ASSERT(i < 2 || !ok);
        }
    }
}


std::pair<TString, TActorId> CmdSetOwner(const ui32 partition, TTestContext& tc, const TString& owner = "default", bool force = true) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    TString cookie;
    TActorId pipeClient;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();

            pipeClient = SetOwner(partition, tc, owner, force);

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            if (result->Record.GetErrorReason().StartsWith("ownership session is killed by another session with id ")) {
                result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);
                UNIT_ASSERT(result);
                UNIT_ASSERT(result->Record.HasStatus());
            }

            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
            UNIT_ASSERT(result->Record.HasPartitionResponse());
            UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdGetOwnershipResult());
            UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdGetOwnershipResult().HasOwnerCookie());
            cookie = result->Record.GetPartitionResponse().GetCmdGetOwnershipResult().GetOwnerCookie();
            UNIT_ASSERT(!cookie.empty());
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
    return std::make_pair(cookie, pipeClient);
}


void WritePartData(const ui32 partition, const TString& sourceId, const i64 offset, const ui64 seqNo, const ui16 partNo, const ui16 totalParts,
                    const ui32 totalSize, const TString& data, TTestContext& tc, const TString& cookie, i32 msgSeqNo)
{
    THolder<TEvPersQueue::TEvRequest> request;
    tc.Runtime->ResetScheduledCount();
    request.Reset(new TEvPersQueue::TEvRequest);
    auto req = request->Record.MutablePartitionRequest();
    req->SetPartition(partition);
    req->SetOwnerCookie(cookie);
    req->SetMessageNo(msgSeqNo);
    if (offset != -1)
        req->SetCmdWriteOffset(offset);
    auto write = req->AddCmdWrite();
    write->SetSourceId(sourceId);
    write->SetSeqNo(seqNo);
    write->SetPartNo(partNo);
    write->SetTotalParts(totalParts);
    if (partNo == 0)
        write->SetTotalSize(totalSize);
    write->SetData(data);

    tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
}

void WritePartDataWithBigMsg(const ui32 partition, const TString& sourceId, const ui64 seqNo, const ui16 partNo, const ui16 totalParts,
                    const ui32 totalSize, const TString& data, TTestContext& tc, const TString& cookie, i32 msgSeqNo, ui32 bigMsgSize)
{
    THolder<TEvPersQueue::TEvRequest> request;
    tc.Runtime->ResetScheduledCount();
    request.Reset(new TEvPersQueue::TEvRequest);
    auto req = request->Record.MutablePartitionRequest();
    req->SetPartition(partition);
    req->SetOwnerCookie(cookie);
    req->SetMessageNo(msgSeqNo);

    TString bigData(bigMsgSize, 'a');

    auto write = req->AddCmdWrite();
    write->SetSourceId(sourceId);
    write->SetSeqNo(seqNo);
    write->SetData(bigData);

    write = req->AddCmdWrite();
    write->SetSourceId(sourceId);
    write->SetSeqNo(seqNo + 1);
    write->SetPartNo(partNo);
    write->SetTotalParts(totalParts);
    if (partNo == 0)
        write->SetTotalSize(totalSize);
    write->SetData(data);


    tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
}



void WriteData(const ui32 partition, const TString& sourceId, const TVector<std::pair<ui64, TString>> data, TTestContext& tc,
               const TString& cookie, i32 msgSeqNo, i64 offset, bool disableDeduplication = false)
{
    THolder<TEvPersQueue::TEvRequest> request;
    tc.Runtime->ResetScheduledCount();
    request.Reset(new TEvPersQueue::TEvRequest);
    auto req = request->Record.MutablePartitionRequest();
    req->SetPartition(partition);
    req->SetOwnerCookie(cookie);
    req->SetMessageNo(msgSeqNo);
    if (offset >= 0)
        req->SetCmdWriteOffset(offset);
    for (auto& p : data) {
        auto write = req->AddCmdWrite();
        write->SetSourceId(sourceId);
        write->SetSeqNo(p.first);
        write->SetData(p.second);
        write->SetDisableDeduplication(disableDeduplication);
    }
    tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
}

void CmdWrite(const ui32 partition, const TString& sourceId, const TVector<std::pair<ui64, TString>> data,
              TTestContext& tc, bool error = false, const THashSet<ui32>& alreadyWrittenSeqNo = {},
              bool isFirst = false, const TString& ownerCookie = "", i32 msn = -1, i64 offset = -1,
              bool treatWrongCookieAsError = false, bool treatBadOffsetAsError = true,
              bool disableDeduplication = false) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;

    ui32& msgSeqNo = tc.MsgSeqNoMap[partition];
    if (msn != -1) msgSeqNo = msn;
    TString cookie = ownerCookie;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            WriteData(partition, sourceId, data, tc, cookie, msgSeqNo, offset, disableDeduplication);
            result = tc.Runtime->GrabEdgeEventIf<TEvPersQueue::TEvResponse>(handle, [](const TEvPersQueue::TEvResponse& ev){
                if (ev.Record.HasPartitionResponse() && ev.Record.GetPartitionResponse().CmdWriteResultSize() > 0 || ev.Record.GetErrorCode() != NPersQueue::NErrorCode::OK)
                    return true;
                return false;
            }); //there could be outgoing reads in TestReadSubscription test

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            if (!treatWrongCookieAsError && result->Record.GetErrorCode() == NPersQueue::NErrorCode::WRONG_COOKIE) {
                cookie = CmdSetOwner(partition, tc).first;
                msgSeqNo = 0;
                retriesLeft = 3;
                continue;
            }

            if (!treatBadOffsetAsError && result->Record.GetErrorCode() == NPersQueue::NErrorCode::WRITE_ERROR_BAD_OFFSET) {
                return;
            }

            if (error) {
                UNIT_ASSERT(result->Record.GetErrorCode() == NPersQueue::NErrorCode::WRITE_ERROR_PARTITION_IS_FULL ||
                            result->Record.GetErrorCode() == NPersQueue::NErrorCode::BAD_REQUEST || result->Record.GetErrorCode() == NPersQueue::NErrorCode::WRONG_COOKIE);
                break;
            } else {
                UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
            }
            UNIT_ASSERT(result->Record.GetPartitionResponse().CmdWriteResultSize() == data.size());

            for (ui32 i = 0; i < data.size(); ++i) {
                UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).HasAlreadyWritten());
                UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).HasOffset());
                UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).HasMaxSeqNo() ==
                                result->Record.GetPartitionResponse().GetCmdWriteResult(i).GetAlreadyWritten());
                if (result->Record.GetPartitionResponse().GetCmdWriteResult(i).HasMaxSeqNo()) {
                    UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).GetMaxSeqNo() >= (i64)data[i].first);
                }
                if (isFirst || offset != -1) {
                    UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).GetAlreadyWritten()
                                      || result->Record.GetPartitionResponse().GetCmdWriteResult(i).GetOffset() == i + (offset == -1 ? 0 : offset));
                }
            }
            for (ui32 i = 0; i < data.size(); ++i) {
                auto res = result->Record.GetPartitionResponse().GetCmdWriteResult(i);
                UNIT_ASSERT(!alreadyWrittenSeqNo.contains(res.GetSeqNo()) || res.GetAlreadyWritten());
            }
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
            retriesLeft = 3;
        }
    }
    ++msgSeqNo;
}


void ReserveBytes(const ui32 partition, TTestContext& tc,
               const TString& cookie, i32 msgSeqNo, i64 size, const TActorId& pipeClient, bool lastRequest)
{
    THolder<TEvPersQueue::TEvRequest> request;
    tc.Runtime->ResetScheduledCount();
    request.Reset(new TEvPersQueue::TEvRequest);
    auto req = request->Record.MutablePartitionRequest();
    req->SetPartition(partition);
    req->SetOwnerCookie(cookie);
    req->SetMessageNo(msgSeqNo);
    ActorIdToProto(pipeClient, req->MutablePipeClient());
    req->MutableCmdReserveBytes()->SetSize(size);
    req->MutableCmdReserveBytes()->SetLastRequest(lastRequest);
    tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());

    tc.Runtime->DispatchEvents();
}


void CmdReserveBytes(const ui32 partition, TTestContext& tc, const TString& ownerCookie, i32 msn, i64 size, TActorId pipeClient, bool noAnswer = false, bool lastRequest = false) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;

    ui32& msgSeqNo = tc.MsgSeqNoMap[partition];
    if (msn != -1) msgSeqNo = msn;
    TString cookie = ownerCookie;

    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            ReserveBytes(partition, tc, cookie, msgSeqNo, size, pipeClient, lastRequest);
            result = tc.Runtime->GrabEdgeEventIf<TEvPersQueue::TEvResponse>(handle, [](const TEvPersQueue::TEvResponse& ev){
                if (!ev.Record.HasPartitionResponse() || !ev.Record.GetPartitionResponse().HasCmdReadResult())
                    return true;
                return false;
            }); //there could be outgoing reads in TestReadSubscription test

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());

            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                retriesLeft = 3;
                continue;
            }

            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::WRONG_COOKIE) {
                auto p = CmdSetOwner(partition, tc);
                pipeClient = p.second;
                cookie = p.first;
                msgSeqNo = 0;
                retriesLeft = 3;
                continue;
            }
            UNIT_ASSERT(!noAnswer);

            UNIT_ASSERT_C(result->Record.GetErrorCode() == NPersQueue::NErrorCode::OK, result->Record);

            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            if (noAnswer)
                break;
            UNIT_ASSERT(retriesLeft == 2);
        }
    }
    ++msgSeqNo;
}


void CmdSetOffset(const ui32 partition, const TString& user, ui64 offset, bool error, TTestContext& tc, const TString& session = "") {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    THolder<TEvPersQueue::TEvRequest> request;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvRequest);
            auto req = request->Record.MutablePartitionRequest();
            req->SetPartition(partition);
            auto off = req->MutableCmdSetClientOffset();
            off->SetClientId(user);
            off->SetOffset(offset);
            if (!session.empty())
                off->SetSessionId(session);
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }
            if ((result->Record.GetErrorCode() == NPersQueue::NErrorCode::SET_OFFSET_ERROR_COMMIT_TO_FUTURE ||
                 result->Record.GetErrorCode() == NPersQueue::NErrorCode::WRONG_COOKIE) && error) {
                break;
            }
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
}


void CmdCreateSession(const ui32 partition, const TString& user, const TString& session, TTestContext& tc, const i64 offset = 0,
                        const ui32 gen = 0, const ui32 step = 0, bool error = false) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    THolder<TEvPersQueue::TEvRequest> request;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvRequest);
            auto req = request->Record.MutablePartitionRequest();
            req->SetPartition(partition);
            auto off = req->MutableCmdCreateSession();
            off->SetClientId(user);
            off->SetSessionId(session);
            off->SetGeneration(gen);
            off->SetStep(step);
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            if (error) {
                UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::WRONG_COOKIE);
                return;
            }

            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

            UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdGetClientOffsetResult());
            auto resp = result->Record.GetPartitionResponse().GetCmdGetClientOffsetResult();
            UNIT_ASSERT(resp.HasOffset() && (i64)resp.GetOffset() == offset);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
}

void CmdKillSession(const ui32 partition, const TString& user, const TString& session, TTestContext& tc) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    THolder<TEvPersQueue::TEvRequest> request;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvRequest);
            auto req = request->Record.MutablePartitionRequest();
            req->SetPartition(partition);
            auto off = req->MutableCmdDeleteSession();
            off->SetClientId(user);
            off->SetSessionId(session);
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
}



void CmdGetOffset(const ui32 partition, const TString& user, i64 offset, TTestContext& tc, i64 ctime = -1, ui64 writeTime = 0) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    THolder<TEvPersQueue::TEvRequest> request;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvRequest);
            auto req = request->Record.MutablePartitionRequest();
            req->SetPartition(partition);
            auto off = req->MutableCmdGetClientOffset();
            off->SetClientId(user);
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());

            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
            UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdGetClientOffsetResult());
            auto resp = result->Record.GetPartitionResponse().GetCmdGetClientOffsetResult();
            if (ctime != -1) {
                UNIT_ASSERT_EQUAL(resp.HasCreateTimestampMS(), ctime > 0);
                if (ctime > 0) {
                    if (ctime == Max<i64>()) {
                        UNIT_ASSERT(resp.GetCreateTimestampMS() + 86000000 < TAppData::TimeProvider->Now().MilliSeconds());
                    } else {
                        UNIT_ASSERT_EQUAL((i64)resp.GetCreateTimestampMS(), ctime);
                    }
                }
            }
            Cerr << "CMDGETOFFSET partition " << partition << " waiting for offset " << offset << ": " << resp << "\n";
            UNIT_ASSERT((offset == -1 && !resp.HasOffset()) || (i64)resp.GetOffset() == offset);
            if (writeTime > 0) {
                UNIT_ASSERT(resp.HasWriteTimestampEstimateMS());
                UNIT_ASSERT(resp.GetWriteTimestampEstimateMS() >= writeTime);
            }
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
}


void CmdUpdateWriteTimestamp(const ui32 partition, ui64 timestamp, TTestContext& tc) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    THolder<TEvPersQueue::TEvRequest> request;
    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvRequest);
            auto req = request->Record.MutablePartitionRequest();
            req->SetPartition(partition);
            auto off = req->MutableCmdUpdateWriteTimestamp();
            off->SetWriteTimeMS(timestamp);
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());

            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }

            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
}


TVector<TString> CmdSourceIdRead(TTestContext& tc) {
    TAutoPtr<IEventHandle> handle;
    TVector<TString> sourceIds;
    THolder<TEvKeyValue::TEvRequest> request;
    TEvKeyValue::TEvResponse *result;

    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            request.Reset(new TEvKeyValue::TEvRequest);
            sourceIds.clear();
            auto read = request->Record.AddCmdReadRange();
            auto range = read->MutableRange();
            NPQ::TKeyPrefix ikeyFrom(NPQ::TKeyPrefix::TypeInfo, 0, NPQ::TKeyPrefix::MarkSourceId);
            range->SetFrom(ikeyFrom.Data(), ikeyFrom.Size());
            range->SetIncludeFrom(true);
            NPQ::TKeyPrefix ikeyTo(NPQ::TKeyPrefix::TypeInfo, 0, NPQ::TKeyPrefix::MarkUserDeprecated);
            range->SetTo(ikeyTo.Data(), ikeyTo.Size());
            range->SetIncludeTo(false);
            Cout << request.Get()->ToString() << Endl;
            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvKeyValue::TEvResponse>(handle);
            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetStatus(), NMsgBusProxy::MSTATUS_OK);
            for (ui64 idx = 0; idx < result->Record.ReadRangeResultSize(); ++idx) {
                const auto &readResult = result->Record.GetReadRangeResult(idx);
                UNIT_ASSERT(readResult.HasStatus());
                UNIT_ASSERT_EQUAL(readResult.GetStatus(), NKikimrProto::OK);
                for (size_t j = 0; j < readResult.PairSize(); ++j) {
                    const auto& pair = readResult.GetPair(j);
                    TString s = pair.GetKey().substr(NPQ::TKeyPrefix::MarkedSize());
                    sourceIds.push_back(s);
                }
            }
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
    return sourceIds;
}


void CmdRead(const ui32 partition, const ui64 offset, const ui32 count, const ui32 size, const ui32 resCount, bool timeouted, TTestContext& tc, TVector<i32> offsets = {}, const ui32 maxTimeLagMs = 0, const ui64 readTimestampMs = 0) {
    TAutoPtr<IEventHandle> handle;
    TEvPersQueue::TEvResponse *result;
    THolder<TEvPersQueue::TEvRequest> request;

    for (i32 retriesLeft = 2; retriesLeft > 0; --retriesLeft) {
        try {
            tc.Runtime->ResetScheduledCount();
            request.Reset(new TEvPersQueue::TEvRequest);
            auto req = request->Record.MutablePartitionRequest();
            req->SetPartition(partition);
            auto read = req->MutableCmdRead();
            read->SetOffset(offset);
            read->SetClientId("user");
            read->SetCount(count);
            read->SetBytes(size);
            if (maxTimeLagMs > 0) {
                read->SetMaxTimeLagMs(maxTimeLagMs);
            }
            if (readTimestampMs > 0) {
                read->SetReadTimestampMs(readTimestampMs);
            }
            req->SetCookie(123);

            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);


            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());

            UNIT_ASSERT(result->Record.HasPartitionResponse());
            UNIT_ASSERT_EQUAL(result->Record.GetPartitionResponse().GetCookie(), 123);
            if (result->Record.GetErrorCode() == NPersQueue::NErrorCode::INITIALIZING) {
                tc.Runtime->DispatchEvents();   // Dispatch events so that initialization can make progress
                retriesLeft = 3;
                continue;
            }
            if (timeouted) {
                UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
                UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdReadResult());
                auto res = result->Record.GetPartitionResponse().GetCmdReadResult();
                UNIT_ASSERT_EQUAL(res.ResultSize(), 0);
                break;
            }
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

            UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdReadResult());
            auto res = result->Record.GetPartitionResponse().GetCmdReadResult();

            UNIT_ASSERT_EQUAL(res.ResultSize(), resCount);
            ui64 off = offset;

            for (ui32 i = 0; i < resCount; ++i) {

                auto r = res.GetResult(i);
                if (offsets.empty()) {
                    if (readTimestampMs == 0) {
                        UNIT_ASSERT_EQUAL((ui64)r.GetOffset(), off);
                    }
                    UNIT_ASSERT(r.GetSourceId().size() == 9 && r.GetSourceId().StartsWith("sourceid"));
                    UNIT_ASSERT_EQUAL(ui32(r.GetData()[0]), off);
                    UNIT_ASSERT_EQUAL(ui32((unsigned char)r.GetData().back()), r.GetSeqNo() % 256);
                    ++off;
                } else {
                    UNIT_ASSERT(offsets[i] == (i64)r.GetOffset());
                }
            }
            retriesLeft = 0;
        } catch (NActors::TSchedulingLimitReachedException) {
            UNIT_ASSERT_VALUES_EQUAL(retriesLeft, 2);
        }
    }
}


void FillUserInfo(NKikimrClient::TKeyValueRequest_TCmdWrite* write, const TString& client, ui32 partition, ui64 offset) {
    NPQ::TKeyPrefix ikey(NPQ::TKeyPrefix::TypeInfo, partition, NPQ::TKeyPrefix::MarkUser);
    ikey.Append(client.c_str(), client.size());

    NKikimrPQ::TUserInfo userInfo;
    userInfo.SetOffset(offset);
    userInfo.SetGeneration(1);
    userInfo.SetStep(2);
    userInfo.SetSession("test-session");
    userInfo.SetOffsetRewindSum(10);
    userInfo.SetReadRuleGeneration(1);
    TString out;
    Y_PROTOBUF_SUPPRESS_NODISCARD userInfo.SerializeToString(&out);

    TBuffer idata;
    idata.Append(out.c_str(), out.size());

    write->SetKey(ikey.Data(), ikey.Size());
    write->SetValue(idata.Data(), idata.Size());
}

void FillDeprecatedUserInfo(NKikimrClient::TKeyValueRequest_TCmdWrite* write, const TString& client, ui32 partition, ui64 offset) {
    TString session = "test-session";
    ui32 gen = 1;
    ui32 step = 2;
    NPQ::TKeyPrefix ikeyDeprecated(NPQ::TKeyPrefix::TypeInfo, partition, NPQ::TKeyPrefix::MarkUserDeprecated);
    ikeyDeprecated.Append(client.c_str(), client.size());

    TBuffer idataDeprecated = NPQ::NDeprecatedUserData::Serialize(offset, gen, step, session);
    write->SetKey(ikeyDeprecated.Data(), ikeyDeprecated.Size());
    write->SetValue(idataDeprecated.Data(), idataDeprecated.Size());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TEST CASES
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


} // NKikimr
