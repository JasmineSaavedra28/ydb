#include "util.h"

#include <util/stream/file.h>
#include <util/string/strip.h>

namespace NYq {

bool IsTerminalStatus(YandexQuery::QueryMeta::ComputeStatus status)
{
    return IsIn({ YandexQuery::QueryMeta::ABORTED_BY_USER, YandexQuery::QueryMeta::ABORTED_BY_SYSTEM,
        YandexQuery::QueryMeta::COMPLETED, YandexQuery::QueryMeta::FAILED }, status);
}

TDuration GetDuration(const TString& value, const TDuration& defaultValue)
{
    TDuration result = defaultValue;
    TDuration::TryParse(value, result);
    return result;
}

NConfig::TControlPlaneStorageConfig FillDefaultParameters(NConfig::TControlPlaneStorageConfig config)
{
    if (!config.GetIdempotencyKeysTtl()) {
        config.SetIdempotencyKeysTtl("10m");
    }

    if (!config.GetMaxRequestSize()) {
        config.SetMaxRequestSize(7 * 1024 * 1024);
    }

    if (!config.GetMaxCountConnections()) {
        config.SetMaxCountConnections(1000000);
    }

    if (!config.GetMaxCountQueries()) {
        config.SetMaxCountQueries(1000000);
    }

    if (!config.GetMaxCountBindings()) {
        config.SetMaxCountBindings(1000000);
    }

    if (!config.GetMaxCountJobs()) {
        config.SetMaxCountJobs(20);
    }

    if (!config.GetTasksBatchSize()) {
        config.SetTasksBatchSize(100);
    }

    if (!config.GetNumTasksProportion()) {
        config.SetNumTasksProportion(4);
    }

    if (!config.GetNumTasksProportion()) {
        config.SetNumTasksProportion(4);
    }

    if (!config.GetAutomaticQueriesTtl()) {
        config.SetAutomaticQueriesTtl("1d");
    }

    if (!config.GetTaskLeaseTtl()) {
        config.SetTaskLeaseTtl("30s");
    }

    if (!config.HasTaskLeaseRetryPolicy()) {
        auto& taskLeaseRetryPolicy = *config.MutableTaskLeaseRetryPolicy();
        taskLeaseRetryPolicy.SetRetryCount(20);
        taskLeaseRetryPolicy.SetRetryPeriod("1d");
    }

    if (!config.RetryPolicyMappingSize()) {
        {
            auto& policyMapping = *config.AddRetryPolicyMapping();
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::UNAVAILABLE);
            auto& policy = *policyMapping.MutablePolicy();
            policy.SetRetryCount(20);
            policy.SetRetryPeriod("1d");
            // backoff is Zero()
        }
        {
            auto& policyMapping = *config.AddRetryPolicyMapping();
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::OVERLOADED);
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::EXTERNAL_ERROR);
            auto& policy = *policyMapping.MutablePolicy();
            policy.SetRetryCount(10);
            policy.SetRetryPeriod("1m");
            policy.SetBackoffPeriod("1s");
        }
    }

    if (!config.GetStorage().GetToken() && config.GetStorage().GetOAuthFile()) {
        config.MutableStorage()->SetToken(StripString(TFileInput(config.GetStorage().GetOAuthFile()).ReadAll()));
    }

    if (!config.GetResultSetsTtl()) {
        config.SetResultSetsTtl("1d");
    }

    return config;
}

bool DoesPingTaskUpdateQueriesTable(const TEvControlPlaneStorage::TEvPingTaskRequest* request) {
    if (!request) {
        return false;
    }
    return request->Status ||
        request->Issues ||
        request->TransientIssues ||
        request->Statistics ||
        request->ResultSetMetas ||
        request->Ast ||
        request->Plan ||
        request->StartedAt ||
        request->FinishedAt ||
        request->ResignQuery ||
        !request->CreatedTopicConsumers.empty() ||
        !request->DqGraphs.empty() ||
        request->DqGraphIndex ||
        request->StateLoadMode ||
        request->StreamingDisposition;
}

NYdb::TValue PackItemsToList(const TVector<NYdb::TValue>& items) {
    NYdb::TValueBuilder itemsAsList;
    itemsAsList.BeginList();
    for (const NYdb::TValue& item: items) {
        itemsAsList.AddListItem(item);
    }
    itemsAsList.EndList();
    return itemsAsList.Build();
}

std::pair<TString, TString> SplitId(const TString& id, char delim) {
    auto it = std::find(id.begin(), id.end(), delim);
    return std::make_pair(id.substr(0, it - id.begin()),
        (it != id.end() ? id.substr(it - id.begin() + 1) : TString{""}));
}

} //namespace NYq
