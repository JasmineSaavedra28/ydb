#include "internal_service.h"
#include "private_client.h"

#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/log.h>

#include <ydb/core/protos/services.pb.h>

#define LOG_E(stream) \
    LOG_ERROR_S(*NActors::TlsActivationContext, NKikimrServices::FQ_INTERNAL_SERVICE, stream)
#define LOG_W(stream) \
    LOG_WARN_S(*NActors::TlsActivationContext, NKikimrServices::FQ_INTERNAL_SERVICE, stream)
#define LOG_I(stream) \
    LOG_INFO_S(*NActors::TlsActivationContext, NKikimrServices::FQ_INTERNAL_SERVICE, stream)
#define LOG_D(stream) \
    LOG_DEBUG_S(*NActors::TlsActivationContext, NKikimrServices::FQ_INTERNAL_SERVICE, stream)

namespace NYq {

NActors::TActorId MakeInternalServiceActorId() {
    constexpr TStringBuf name = "FQINTSRV";
    return NActors::TActorId(0, name);
}

class TInternalService : public NActors::TActorBootstrapped<TInternalService> {
public:
    TInternalService(
        const NYq::TYqSharedResources::TPtr& yqSharedResources,
        const NKikimr::TYdbCredentialsProviderFactory& credentialsProviderFactory,
        const ::NYq::NConfig::TPrivateApiConfig& privateApiConfig,
        const NMonitoring::TDynamicCounterPtr& counters)
        : ServiceCounters(counters->GetSubgroup("subsystem", "InternalService"))
        , EventLatency(ServiceCounters->GetSubgroup("subcomponent", "Latency")->GetHistogram("Latency", NMonitoring::ExponentialHistogram(10, 2, 50)))
        , PrivateClient(
            yqSharedResources->CoreYdbDriver,
            NYdb::TCommonClientSettings()
                .DiscoveryEndpoint(privateApiConfig.GetTaskServiceEndpoint())
                .CredentialsProviderFactory(credentialsProviderFactory({.SaKeyFile = privateApiConfig.GetSaKeyFile(), .IamEndpoint = privateApiConfig.GetIamEndpoint()}))
                .EnableSsl(privateApiConfig.GetSecureTaskService())
                .Database(privateApiConfig.GetTaskServiceDatabase() ? privateApiConfig.GetTaskServiceDatabase() : TMaybe<TString>()),
            counters)
    {
    }

    static constexpr char ActorName[] = "FQ_INTERNAL_SERVICE";

    void Bootstrap() {
        Become(&TInternalService::StateFunc);
        LOG_I("STARTED");
    }

private:
    STRICT_STFUNC(StateFunc,
        hFunc(TEvInternalService::TEvHealthCheckRequest, Handle)
        hFunc(TEvInternalService::TEvGetTaskRequest, Handle)
        hFunc(TEvInternalService::TEvPingTaskRequest, Handle)
        hFunc(TEvInternalService::TEvWriteResultRequest, Handle)
    );

    void Handle(TEvInternalService::TEvHealthCheckRequest::TPtr& ev) {
        EventLatency->Collect((TInstant::Now() - ev->Get()->SentAt).MilliSeconds());
        PrivateClient
            .NodesHealthCheck(std::move(ev->Get()->Request))
            .Subscribe([actorSystem = NActors::TActivationContext::ActorSystem(), senderId = ev->Sender, selfId = SelfId(), cookie = ev->Cookie](const NThreading::TFuture<TNodesHealthCheckResult>& future) {
                try {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvHealthCheckResponse(future.GetValue()), 0, cookie));
                } catch (...) {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvHealthCheckResponse(CurrentExceptionMessage()), 0, cookie));
                }
            });
    }

    void Handle(TEvInternalService::TEvGetTaskRequest::TPtr& ev) {
        EventLatency->Collect((TInstant::Now() - ev->Get()->SentAt).MilliSeconds());
        PrivateClient
            .GetTask(std::move(ev->Get()->Request))
            .Subscribe([actorSystem = NActors::TActivationContext::ActorSystem(), senderId = ev->Sender, selfId = SelfId(), cookie = ev->Cookie](const NThreading::TFuture<TGetTaskResult>& future) {
                try {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvGetTaskResponse(future.GetValue()), 0, cookie));
                } catch (...) {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvGetTaskResponse(CurrentExceptionMessage()), 0, cookie));
                }
            });
    }

    void Handle(TEvInternalService::TEvPingTaskRequest::TPtr& ev) {
        EventLatency->Collect((TInstant::Now() - ev->Get()->SentAt).MilliSeconds());
        PrivateClient
            .PingTask(std::move(ev->Get()->Request))
            .Subscribe([actorSystem = NActors::TActivationContext::ActorSystem(), senderId = ev->Sender, selfId = SelfId(), cookie = ev->Cookie](const NThreading::TFuture<TPingTaskResult>& future) {
                try {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvPingTaskResponse(future.GetValue()), 0, cookie));
                } catch (...) {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvPingTaskResponse(CurrentExceptionMessage()), 0, cookie));
                }
            });
    }

    void Handle(TEvInternalService::TEvWriteResultRequest::TPtr& ev) {
        EventLatency->Collect((TInstant::Now() - ev->Get()->SentAt).MilliSeconds());
        PrivateClient
            .WriteTaskResult(std::move(ev->Get()->Request))
            .Subscribe([actorSystem = NActors::TActivationContext::ActorSystem(), senderId = ev->Sender, selfId = SelfId(), cookie = ev->Cookie](const auto& future) {
                try {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvWriteResultResponse(future.GetValue()), 0, cookie));
                } catch (...) {
                    actorSystem->Send(new NActors::IEventHandle(senderId, selfId, new TEvInternalService::TEvWriteResultResponse(CurrentExceptionMessage()), 0, cookie));
                }
            });
    }

    const NMonitoring::TDynamicCounterPtr ServiceCounters;
    const NMonitoring::THistogramPtr EventLatency;
    TPrivateClient PrivateClient;
};

NActors::IActor* CreateInternalServiceActor(
    const NYq::TYqSharedResources::TPtr& yqSharedResources,
    const NKikimr::TYdbCredentialsProviderFactory& credentialsProviderFactory,
    const NYq::NConfig::TPrivateApiConfig& privateApiConfig,
    const NMonitoring::TDynamicCounterPtr& counters) {
        return new TInternalService(yqSharedResources, credentialsProviderFactory, privateApiConfig, counters);
}

} /* NYq */
