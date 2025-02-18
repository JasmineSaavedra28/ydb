#include "grpc_pq_read.h"

#include "actors/read_info_actor.h"

#include <ydb/core/grpc_services/grpc_helper.h>
#include <ydb/core/tx/scheme_board/cache.h>

#include <algorithm>

using namespace NActors;
using namespace NKikimrClient;

using grpc::Status;

namespace NKikimr {
namespace NGRpcProxy {
namespace V1 {

///////////////////////////////////////////////////////////////////////////////

using namespace PersQueue::V1;



IActor* CreatePQReadService(const TActorId& schemeCache, const TActorId& newSchemeCache,
                             TIntrusivePtr<NMonitoring::TDynamicCounters> counters, const ui32 maxSessions) {
    return new TPQReadService(schemeCache, newSchemeCache, counters, maxSessions);
}



TPQReadService::TPQReadService(const TActorId& schemeCache, const TActorId& newSchemeCache,
                             TIntrusivePtr<NMonitoring::TDynamicCounters> counters, const ui32 maxSessions)
    : SchemeCache(schemeCache)
    , NewSchemeCache(newSchemeCache)
    , Counters(counters)
    , MaxSessions(maxSessions)
    , LocalCluster("")
{
}


void TPQReadService::Bootstrap(const TActorContext& ctx) {
    HaveClusters = !AppData(ctx)->PQConfig.GetTopicsAreFirstClassCitizen(); // ToDo[migration] - proper condition
    if (HaveClusters) {
        ctx.Send(NPQ::NClusterTracker::MakeClusterTrackerID(),
                 new NPQ::NClusterTracker::TEvClusterTracker::TEvSubscribe);
    } else {
        TopicConverterFactory = std::make_shared<NPersQueue::TTopicNamesConverterFactory>(
                AppData(ctx)->PQConfig, ""
        );
        TopicsHandler = std::make_unique<NPersQueue::TTopicsListController>(
                TopicConverterFactory
        );
    }
    ctx.Send(NNetClassifier::MakeNetClassifierID(), new NNetClassifier::TEvNetClassifier::TEvSubscribe);
    Become(&TThis::StateFunc);
}


ui64 TPQReadService::NextCookie() {
    return ++LastCookie;
}


void TPQReadService::Handle(NNetClassifier::TEvNetClassifier::TEvClassifierUpdate::TPtr& ev, const TActorContext& ctx) {
    if (!DatacenterClassifier) {
        for (auto it = Sessions.begin(); it != Sessions.end(); ++it) {
            ctx.Send(it->second, new TEvPQProxy::TEvDieCommand("datacenter classifier initialized, restart session please", PersQueue::ErrorCode::INITIALIZING));
        }
    }

    DatacenterClassifier = ev->Get()->Classifier;
}

void TPQReadService::Handle(NPQ::NClusterTracker::TEvClusterTracker::TEvClustersUpdate::TPtr& ev, const TActorContext& ctx) {
    Y_VERIFY(ev->Get()->ClustersList);

    Y_VERIFY(ev->Get()->ClustersList->Clusters.size());

    const auto& clusters = ev->Get()->ClustersList->Clusters;

    LocalCluster = {};

    auto it = std::find_if(begin(clusters), end(clusters), [](const auto& cluster) { return cluster.IsLocal; });
    if (it != end(clusters)) {
        LocalCluster = it->Name;
    }

    Clusters.resize(clusters.size());
    for (size_t i = 0; i < clusters.size(); ++i) {
        Clusters[i] = clusters[i].Name;
    }
    if (TopicConverterFactory == nullptr) {
        TopicConverterFactory = std::make_shared<NPersQueue::TTopicNamesConverterFactory>(
                AppData(ctx)->PQConfig, LocalCluster
        );
        TopicsHandler = std::make_unique<NPersQueue::TTopicsListController>(
                TopicConverterFactory, Clusters
        );
    }
    TopicsHandler->UpdateClusters(Clusters);
}


void TPQReadService::Handle(TEvPQProxy::TEvSessionDead::TPtr& ev, const TActorContext&) {
    Sessions.erase(ev->Get()->Cookie);
}

google::protobuf::RepeatedPtrField<Ydb::Issue::IssueMessage> FillInfoResponse(const TString& errorReason, const PersQueue::ErrorCode::ErrorCode code) {
    google::protobuf::RepeatedPtrField<Ydb::Issue::IssueMessage> res;
    FillIssue(res.Add(), code, errorReason);
    return res;
}

void TPQReadService::Handle(NGRpcService::TEvStreamPQReadRequest::TPtr& ev, const TActorContext& ctx) {
    HandleStreamPQReadRequest<NGRpcService::TEvStreamPQReadRequest>(ev, ctx);
}

void TPQReadService::Handle(NGRpcService::TEvStreamPQMigrationReadRequest::TPtr& ev, const TActorContext& ctx) {
    HandleStreamPQReadRequest<NGRpcService::TEvStreamPQMigrationReadRequest>(ev, ctx);
}

void TPQReadService::Handle(NGRpcService::TEvPQReadInfoRequest::TPtr& ev, const TActorContext& ctx) {

    LOG_DEBUG_S(ctx, NKikimrServices::PQ_READ_PROXY, "new read info request");

    if (Clusters.empty() || LocalCluster.empty()) {
        LOG_INFO_S(ctx, NKikimrServices::PQ_READ_PROXY, "new read info request failed - cluster is not known yet");

        ev->Get()->SendResult(ConvertPersQueueInternalCodeToStatus(PersQueue::ErrorCode::INITIALIZING), FillInfoResponse("cluster initializing", PersQueue::ErrorCode::INITIALIZING)); //CANCELLED
        return;
    } else {
        //ctx.Register(new TReadInfoActor(ev->Release().Release(), Clusters, LocalCluster, SchemeCache, NewSchemeCache, Counters));
        ctx.Register(new TReadInfoActor(ev->Release().Release(), *TopicsHandler, SchemeCache, NewSchemeCache, Counters));
    }
}



bool TPQReadService::TooMuchSessions() {
    return Sessions.size() >= MaxSessions;
}


}
}
}


void NKikimr::NGRpcService::TGRpcRequestProxy::Handle(NKikimr::NGRpcService::TEvStreamPQReadRequest::TPtr& ev, const TActorContext& ctx) {
    ctx.Send(NKikimr::NGRpcProxy::V1::GetPQReadServiceActorID(), ev->Release().Release());
}

void NKikimr::NGRpcService::TGRpcRequestProxy::Handle(NKikimr::NGRpcService::TEvStreamPQMigrationReadRequest::TPtr& ev, const TActorContext& ctx) {
    ctx.Send(NKikimr::NGRpcProxy::V1::GetPQReadServiceActorID(), ev->Release().Release());
}

void NKikimr::NGRpcService::TGRpcRequestProxy::Handle(NKikimr::NGRpcService::TEvPQReadInfoRequest::TPtr& ev, const TActorContext& ctx) {
    ctx.Send(NKikimr::NGRpcProxy::V1::GetPQReadServiceActorID(), ev->Release().Release());
}
