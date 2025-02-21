#pragma once
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/actors/core/events.h>
#include <library/cpp/actors/core/event_local.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/log.h>
#include <library/cpp/actors/interconnect/poller_actor.h>
#include <library/cpp/dns/cache.h>
#include <library/cpp/monlib/metrics/metric_registry.h>
#include <util/generic/variant.h>
#include "http.h"
#include "http_proxy_ssl.h"

namespace NHttp {

struct TSocketDescriptor : NActors::TSharedDescriptor, THttpConfig {
    SocketType Socket;

    int GetDescriptor() override {
        return static_cast<SOCKET>(Socket);
    }
};

struct TEvHttpProxy {
    enum EEv {
        EvAddListeningPort = EventSpaceBegin(NActors::TEvents::ES_HTTP),
        EvConfirmListen,
        EvRegisterHandler,
        EvHttpIncomingRequest,
        EvHttpOutgoingRequest,
        EvHttpIncomingResponse,
        EvHttpOutgoingResponse,
        EvHttpConnectionOpened,
        EvHttpConnectionClosed,
        EvHttpAcceptorClosed,
        EvResolveHostRequest,
        EvResolveHostResponse,
        EvReportSensors,
        EvEnd
    };

    static_assert(EvEnd < EventSpaceEnd(NActors::TEvents::ES_HTTP), "ES_HTTP event space is too small.");

    struct TEvAddListeningPort : NActors::TEventLocal<TEvAddListeningPort, EvAddListeningPort> {
        TIpPort Port;
        TString WorkerName;
        bool Secure = false;
        TString CertificateFile;
        TString PrivateKeyFile;
        TString SslCertificatePem;

        TEvAddListeningPort(TIpPort port)
            : Port(port)
        {}

        TEvAddListeningPort(TIpPort port, const TString& workerName)
            : Port(port)
            , WorkerName(workerName)
        {}
    };

    struct TEvConfirmListen : NActors::TEventLocal<TEvConfirmListen, EvConfirmListen> {
        THttpConfig::SocketAddressType Address;

        TEvConfirmListen(const THttpConfig::SocketAddressType& address)
            : Address(address)
        {}
    };

    struct TEvRegisterHandler : NActors::TEventLocal<TEvRegisterHandler, EvRegisterHandler> {
        TString Path;
        TActorId Handler;

        TEvRegisterHandler(const TString& path, const TActorId& handler)
            : Path(path)
            , Handler(handler)
        {}
    };

    struct TEvHttpIncomingRequest : NActors::TEventLocal<TEvHttpIncomingRequest, EvHttpIncomingRequest> {
        THttpIncomingRequestPtr Request;

        TEvHttpIncomingRequest(THttpIncomingRequestPtr request)
            : Request(std::move(request))
        {}
    };

    struct TEvHttpOutgoingRequest : NActors::TEventLocal<TEvHttpOutgoingRequest, EvHttpOutgoingRequest> {
        THttpOutgoingRequestPtr Request;
        TDuration Timeout;

        TEvHttpOutgoingRequest(THttpOutgoingRequestPtr request)
            : Request(std::move(request))
        {}

        TEvHttpOutgoingRequest(THttpOutgoingRequestPtr request, TDuration timeout)
            : Request(std::move(request))
            , Timeout(timeout)
        {}
    };

    struct TEvHttpIncomingResponse : NActors::TEventLocal<TEvHttpIncomingResponse, EvHttpIncomingResponse> {
        THttpOutgoingRequestPtr Request;
        THttpIncomingResponsePtr Response;
        TString Error;

        TEvHttpIncomingResponse(THttpOutgoingRequestPtr request, THttpIncomingResponsePtr response, const TString& error)
            : Request(std::move(request))
            , Response(std::move(response))
            , Error(error)
        {}

        TEvHttpIncomingResponse(THttpOutgoingRequestPtr request, THttpIncomingResponsePtr response)
            : Request(std::move(request))
            , Response(std::move(response))
        {}

        TString GetError() const {
            TStringBuilder error;
            if (Response != nullptr && !Response->Status.StartsWith('2')) {
                error << Response->Status << ' ' << Response->Message;
            }
            if (!Error.empty()) {
                if (!error.empty()) {
                    error << ';';
                }
                error << Error;
            }
            return error;
        }
    };

    struct TEvHttpOutgoingResponse : NActors::TEventLocal<TEvHttpOutgoingResponse, EvHttpOutgoingResponse> {
        THttpOutgoingResponsePtr Response;

        TEvHttpOutgoingResponse(THttpOutgoingResponsePtr response)
            : Response(std::move(response))
        {}
    };

    struct TEvHttpConnectionOpened : NActors::TEventLocal<TEvHttpConnectionOpened, EvHttpConnectionOpened> {
        TString PeerAddress;
        TActorId ConnectionID;

        TEvHttpConnectionOpened(const TString& peerAddress, const TActorId& connectionID)
            : PeerAddress(peerAddress)
            , ConnectionID(connectionID)
        {}
    };

    struct TEvHttpConnectionClosed : NActors::TEventLocal<TEvHttpConnectionClosed, EvHttpConnectionClosed> {
        TActorId ConnectionID;
        TDeque<THttpIncomingRequestPtr> RecycledRequests;

        TEvHttpConnectionClosed(const TActorId& connectionID)
            : ConnectionID(connectionID)
        {}

        TEvHttpConnectionClosed(const TActorId& connectionID, TDeque<THttpIncomingRequestPtr> recycledRequests)
            : ConnectionID(connectionID)
            , RecycledRequests(std::move(recycledRequests))
        {}
    };

    struct TEvHttpAcceptorClosed : NActors::TEventLocal<TEvHttpAcceptorClosed, EvHttpAcceptorClosed> {
        TActorId ConnectionID;

        TEvHttpAcceptorClosed(const TActorId& connectionID)
            : ConnectionID(connectionID)
        {}
    };

    struct TEvResolveHostRequest : NActors::TEventLocal<TEvResolveHostRequest, EvResolveHostRequest> {
        TString Host;

        TEvResolveHostRequest(const TString& host)
            : Host(host)
        {}
    };

    struct TEvResolveHostResponse : NActors::TEventLocal<TEvResolveHostResponse, EvResolveHostResponse> {
        TString Host;
        TSockAddrInet6 Address;
        TString Error;

        TEvResolveHostResponse(const TString& host, const TSockAddrInet6& address)
            : Host(host)
            , Address(address)
        {}

        TEvResolveHostResponse(const TString& error)
            : Error(error)
        {}
    };

    struct TEvReportSensors : NActors::TEventLocal<TEvReportSensors, EvReportSensors> {
        TString Direction;
        TString Host;
        TString Url;
        TString Status;
        TDuration Time;

        TEvReportSensors(
                TStringBuf direction,
                TStringBuf host,
                TStringBuf url,
                TStringBuf status,
                TDuration time)
            : Direction(direction)
            , Host(host)
            , Url(url)
            , Status(status)
            , Time(time)
        {}
    };
};

struct TEndpointInfo {
    TActorId Proxy;
    TActorId Owner;
    TString WorkerName;
    bool Secure;
    TSslHelpers::TSslHolder<SSL_CTX> SecureContext;
};

NActors::IActor* CreateHttpProxy(std::weak_ptr<NMonitoring::TMetricRegistry> registry = NMonitoring::TMetricRegistry::SharedInstance());
NActors::IActor* CreateHttpAcceptorActor(const TActorId& owner, const TActorId& poller);
NActors::IActor* CreateOutgoingConnectionActor(const TActorId& owner, const TString& host, bool secure, const TActorId& poller);
NActors::IActor* CreateIncomingConnectionActor(
        const TEndpointInfo& endpoint,
        TIntrusivePtr<TSocketDescriptor> socket,
        THttpConfig::SocketAddressType address,
        THttpIncomingRequestPtr recycledRequest = nullptr);
TEvHttpProxy::TEvReportSensors* BuildOutgoingRequestSensors(const THttpOutgoingRequestPtr& request, const THttpIncomingResponsePtr& response);
TEvHttpProxy::TEvReportSensors* BuildIncomingRequestSensors(const THttpIncomingRequestPtr& request, const THttpOutgoingResponsePtr& response);

}
