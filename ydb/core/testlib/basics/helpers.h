#pragma once

#include "appdata.h"
#include "runtime.h"
#include <ydb/core/util/defs.h>
#include <ydb/core/base/blobstorage.h>
#include <ydb/core/blobstorage/nodewarden/node_warden.h>
#include <ydb/core/blobstorage/pdisk/blobstorage_pdisk.h>
#include <ydb/core/blobstorage/pdisk/blobstorage_pdisk_factory.h>

#include <functional>

namespace NKikimr {
namespace NFake {
    struct TStorage {
        bool UseDisk = false;
        ui64 SectorSize = 0;
        ui64 ChunkSize = 0;
        ui64 DiskSize = 0;
    };

    struct TCaches {
        ui64 Shared = 32 * (ui64(1) << 20); // Shared cache limit, bytes
        ui64 ScanQueue = 512 * (ui64(1) << 20); // Scan queue in flight limit, bytes
        ui64 AsyncQueue = 512 * (ui64(1) << 20); // Async queue in flight limit, bytes
    };

    struct INode {
        virtual void Birth(ui32 node) noexcept = 0;
    };
}

    const TBlobStorageGroupType::EErasureSpecies BootGroupErasure = TBlobStorageGroupType::ErasureNone;

    TTabletStorageInfo* CreateTestTabletInfo(ui64 tabletId, TTabletTypes::EType tabletType,
        TBlobStorageGroupType::EErasureSpecies erasure = BootGroupErasure, ui32 groupId = 0);
    TActorId CreateTestBootstrapper(TTestActorRuntime &runtime, TTabletStorageInfo *info,
        std::function<IActor* (const TActorId &, TTabletStorageInfo*)> op, ui32 nodeIndex = 0);
    NTabletPipe::TClientConfig GetPipeConfigWithRetries();

    void SetupStateStorage(TTestActorRuntime& runtime, ui32 nodeIndex,
                           ui64 stateStorageGroup = 0, bool replicasOnFirstNode = false);
    void SetupBSNodeWarden(TTestActorRuntime& runtime, ui32 nodeIndex, TIntrusivePtr<TNodeWardenConfig> nodeWardenConfig);
    void SetupTabletResolver(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupTabletPipePeNodeCaches(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupResourceBroker(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupSharedPageCache(TTestActorRuntime& runtime, ui32 nodeIndex, NFake::TCaches caches);
    void SetupNodeWhiteboard(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupMonitoringProxy(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupGRpcProxyStatus(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupNodeTabletMonitor(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupSchemeCache(TTestActorRuntime& runtime, ui32 nodeIndex, const TString& root);
    void SetupQuoterService(TTestActorRuntime& runtime, ui32 nodeIndex);
    void SetupSysViewService(TTestActorRuntime& runtime, ui32 nodeIndex);

    // StateStorage, NodeWarden, TabletResolver, ResourceBroker, SharedPageCache
    void SetupBasicServices(TTestActorRuntime &runtime, TAppPrepare &app, bool mockDisk = false,
                            NFake::INode *factory = nullptr, NFake::TStorage storage = {}, NFake::TCaches caches = {});

    ///
    class TStrandedPDiskServiceFactory : public IPDiskServiceFactory {
        TTestActorRuntime &Runtime;
    public:
        TStrandedPDiskServiceFactory(TTestActorRuntime &runtime)
            : Runtime(runtime)
        {}

        void Create(const TActorContext &ctx, ui32 pDiskID, const TIntrusivePtr<TPDiskConfig> &cfg,
            const NPDisk::TKey &mainKey, ui32 poolId, ui32 nodeId) override;

        virtual ~TStrandedPDiskServiceFactory()
        {}
    };

}
