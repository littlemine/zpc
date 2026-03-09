#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "zensim/execution/AsyncRuntimeAbi.hpp"

namespace {

  struct DemoTaskState {
    int callCount{0};
  };

  struct DemoNativeQueueState {
    void *queueHandle{reinterpret_cast<void *>(static_cast<uintptr_t>(0xCAFE))};
    void *signalHandle{reinterpret_cast<void *>(static_cast<uintptr_t>(0xBEEF))};
    int syncCount{0};
    int recordCount{0};
    int waitCount{0};
    std::vector<void *> waitedSignals{};
  };

  struct DemoResourcePayload {
    int value{7};
  };

  struct DemoResourceState {
    int maintenanceCount{0};
    int destroyCount{0};
  };

  zpc_runtime_host_task_result_e demo_task(void *user_data,
                                           const zpc_runtime_host_task_context_t *context,
                                           const zpc_runtime_submission_desc_t *desc) {
    auto *state = static_cast<DemoTaskState *>(user_data);
    if (state) ++state->callCount;
    std::cout << "task callback: submission_id=" << context->submission_id
              << " label=" << std::string{desc->task_label.data, desc->task_label.size}
              << " stop=" << context->stop_requested
              << " interrupt=" << context->interrupt_requested << "\n";
    return ZPC_RUNTIME_HOST_TASK_COMPLETED;
  }

  void *demo_queue_handle(void *binding) {
    return static_cast<DemoNativeQueueState *>(binding)->queueHandle;
  }

  void *demo_signal_handle(void *binding) {
    return static_cast<DemoNativeQueueState *>(binding)->signalHandle;
  }

  int32_t demo_sync(void *binding) {
    ++static_cast<DemoNativeQueueState *>(binding)->syncCount;
    return 1;
  }

  int32_t demo_record(void *binding) {
    ++static_cast<DemoNativeQueueState *>(binding)->recordCount;
    return 1;
  }

  int32_t demo_wait(void *binding, void *foreignSignal) {
    auto *state = static_cast<DemoNativeQueueState *>(binding);
    ++state->waitCount;
    state->waitedSignals.push_back(foreignSignal);
    return 1;
  }

  zpc_runtime_host_task_result_e demo_resource_maintain(
      void *user_data, const zpc_runtime_resource_maintenance_context_v1_t *context) {
    auto *state = static_cast<DemoResourceState *>(user_data);
    auto *payload = static_cast<DemoResourcePayload *>(context->payload);
    if (state) ++state->maintenanceCount;
    std::cout << "resource maintenance: handle=" << context->resource_handle
              << " epoch=" << context->epoch
              << " kind=" << context->maintenance_kind
              << " payload=" << (payload ? payload->value : -1) << "\n";
    return ZPC_RUNTIME_HOST_TASK_COMPLETED;
  }

  void demo_resource_destroy(void *user_data, void *payload) {
    auto *state = static_cast<DemoResourceState *>(user_data);
    auto *resource = static_cast<DemoResourcePayload *>(payload);
    if (state) ++state->destroyCount;
    std::cout << "resource destroy: payload=" << (resource ? resource->value : -1) << "\n";
    delete resource;
  }

  std::string to_string(zpc_runtime_string_view_t view) {
    if (!view.data || view.size == 0) return {};
    return std::string{view.data, view.size};
  }

}  // namespace

int main() {
  using namespace zs;

  AsyncRuntimeAbiEngineConfig config{};
  config.engineName = "zpc-abi-demo";
  config.buildId = "demo";
  auto *engine = make_async_runtime_abi_engine(config);
  const auto *engineTable = async_runtime_abi_engine_table(engine);

  zpc_runtime_engine_desc_t engineDesc{};
  engineDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_desc_t));
  engineTable->query_engine_desc(engine, &engineDesc);
  std::cout << "engine: name=" << to_string(engineDesc.engine_name)
            << " build=" << to_string(engineDesc.build_id)
            << " caps=" << engineDesc.capability_mask << "\n";

  zpc_runtime_extension_desc_t hostExtensionDesc{};
  hostExtensionDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
  engineTable->query_extension(engine, zpc_runtime_make_string_view(zpc_runtime_host_submit_extension_name),
                               &hostExtensionDesc);
  std::cout << "extension: " << to_string(hostExtensionDesc.extension_name)
            << " version=" << hostExtensionDesc.extension_version_major
            << "." << hostExtensionDesc.extension_version_minor << "\n";

  DemoTaskState hostTaskState{};
  zpc_runtime_host_submit_payload_t hostPayload{};
  hostPayload.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
  hostPayload.task = &demo_task;
  hostPayload.user_data = &hostTaskState;

  zpc_runtime_submission_desc_t submitDesc{};
  submitDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_submission_desc_t));
  submitDesc.executor_name = zpc_runtime_make_string_view("inline");
  submitDesc.task_label = zpc_runtime_make_string_view("demo-host-task");
  submitDesc.domain_code = static_cast<uint32_t>(AsyncDomain::compute);
  submitDesc.queue_code = static_cast<uint32_t>(AsyncQueueClass::compute);
  submitDesc.backend_code = static_cast<uint32_t>(AsyncBackend::inline_host);
  submitDesc.priority = 1;
  submitDesc.reserved[zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&hostPayload);

  zpc_runtime_submission_handle_t *hostSubmission = nullptr;
  engineTable->submit(engine, &submitDesc, &hostSubmission);

  zpc_runtime_host_event_t hostEvent{};
  hostEvent.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
  engineTable->query_event(hostSubmission, &hostEvent);
  std::cout << "host event: status=" << hostEvent.status_code
            << " token=" << hostEvent.native_signal_token
            << " calls=" << hostTaskState.callCount << "\n";
  engineTable->release_submission(hostSubmission);

    DemoTaskState dependentHostTaskState{};
    zpc_runtime_host_submit_payload_t dependentHostPayload{};
    dependentHostPayload.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
    dependentHostPayload.task = &demo_task;
    dependentHostPayload.user_data = &dependentHostTaskState;

    zpc_runtime_dependency_token_v1_t dependentHostToken{};
    dependentHostToken.token = hostEvent.native_signal_token;
    dependentHostToken.kind = ZPC_RUNTIME_DEPENDENCY_SUBMISSION_EVENT;
    zpc_runtime_dependency_list_v1_t dependentHostList{};
    dependentHostList.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_dependency_list_v1_t));
    dependentHostList.items = &dependentHostToken;
    dependentHostList.count = 1;

    zpc_runtime_submission_desc_t dependentHostDesc = submitDesc;
    dependentHostDesc.task_label = zpc_runtime_make_string_view("demo-dependent-host-task");
    dependentHostDesc.reserved[zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&dependentHostPayload);
    dependentHostDesc.reserved[zpc_runtime_dependency_list_payload_slot] =
      reinterpret_cast<uint64_t>(&dependentHostList);

    zpc_runtime_submission_handle_t *dependentHostSubmission = nullptr;
    engineTable->submit(engine, &dependentHostDesc, &dependentHostSubmission);
    std::cout << "dependent host: prerequisite_token=" << dependentHostToken.token
        << " calls=" << dependentHostTaskState.callCount << "\n";
    engineTable->release_submission(dependentHostSubmission);

  ValidationSuiteReport report{};
  report.suite = "abi-demo";
  ValidationRecord record{};
  record.recordId = "abi.demo.host";
  record.suite = "abi-demo";
  record.name = "host-export";
  record.backend = "inline_host";
  record.executor = "inline";
  record.target = "cpu";
  record.outcome = ValidationOutcome::pass;
  record.measurements.push_back(ValidationMeasurement{
      "latency", "ns", 12.0,
      ValidationThreshold{ValidationThresholdMode::less_equal, 64.0, 0.0}});
  report.records.push_back(record);
  publish_async_runtime_validation_report(engine, report);

  zpc_runtime_extension_desc_t validationExtensionDesc{};
  validationExtensionDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
  engineTable->query_extension(engine,
                               zpc_runtime_make_string_view(zpc_runtime_validation_extension_name),
                               &validationExtensionDesc);
  const auto *validationExtension =
      static_cast<const zpc_runtime_validation_extension_v1_t *>(validationExtensionDesc.function_table);

    zpc_runtime_extension_desc_t resourceExtensionDesc{};
    resourceExtensionDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
    engineTable->query_extension(
      engine,
      zpc_runtime_make_string_view(zpc_runtime_resource_manager_extension_name),
      &resourceExtensionDesc);
    std::cout << "extension: " << to_string(resourceExtensionDesc.extension_name)
        << " version=" << resourceExtensionDesc.extension_version_major
        << "." << resourceExtensionDesc.extension_version_minor << "\n";
    const auto *resourceExtension =
      static_cast<const zpc_runtime_resource_manager_extension_v1_t *>(
        resourceExtensionDesc.function_table);

  zpc_runtime_validation_summary_v1_t summary{};
  summary.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_validation_summary_v1_t));
  validationExtension->query_summary(engine, &summary);
  std::cout << "validation summary: suite=" << to_string(summary.suite)
            << " total=" << summary.total
            << " passed=" << summary.passed << "\n";

  zpc_runtime_string_view_t jsonView{};
  validationExtension->query_json(engine, &jsonView);
  std::cout << "validation json: " << to_string(jsonView) << "\n";

  ValidationSuiteReport baselineReport = report;
  baselineReport.records[0].outcome = ValidationOutcome::fail;
  baselineReport.records[0].measurements[0].value = 80.0;
  const auto baselinePath = std::filesystem::temp_directory_path()
                          / "zpc_async_runtime_abi_demo_baseline.json";
  std::error_code removeError;
  std::filesystem::remove(baselinePath, removeError);
  std::string persistenceError;
  if (save_validation_report_json_file(baselineReport, baselinePath.string(), &persistenceError)
      && validationExtension->compare_baseline_file(
             engine, zpc_runtime_make_string_view(baselinePath.string().c_str()))
             == ZPC_RUNTIME_ABI_OK) {
    zpc_runtime_validation_comparison_summary_v1_t comparisonSummary{};
    comparisonSummary.header = zpc_runtime_make_header(
        (uint32_t)sizeof(zpc_runtime_validation_comparison_summary_v1_t));
    validationExtension->query_comparison_summary(engine, &comparisonSummary);
    std::cout << "validation comparison: suite=" << to_string(comparisonSummary.suite)
              << " accepted=" << comparisonSummary.accepted
              << " improved=" << comparisonSummary.improved
              << " regressed=" << comparisonSummary.regressed << "\n";

    zpc_runtime_string_view_t comparisonJsonView{};
    validationExtension->query_comparison_json(engine, &comparisonJsonView);
    std::cout << "validation comparison json: " << to_string(comparisonJsonView) << "\n";
  } else {
    std::cout << "validation comparison unavailable: " << persistenceError << "\n";
  }
  std::filesystem::remove(baselinePath, removeError);

    DemoResourceState resourceState{};
    zpc_runtime_resource_desc_v1_t resourceDesc{};
    resourceDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_resource_desc_v1_t));
    resourceDesc.resource_label = zpc_runtime_make_string_view("demo-resource");
    resourceDesc.executor_name = zpc_runtime_make_string_view("inline");
    resourceDesc.domain_code = static_cast<uint32_t>(AsyncDomain::compute);
    resourceDesc.queue_code = static_cast<uint32_t>(AsyncQueueClass::compute);
    resourceDesc.backend_code = static_cast<uint32_t>(AsyncBackend::inline_host);
    resourceDesc.priority = 2;
    resourceDesc.bytes = sizeof(DemoResourcePayload);
    resourceDesc.stale_after_epochs = 2;
    resourceDesc.payload = new DemoResourcePayload{};
    resourceDesc.user_data = &resourceState;
    resourceDesc.maintain = &demo_resource_maintain;
    resourceDesc.destroy = &demo_resource_destroy;

    uint64_t resourceHandle = 0;
    resourceExtension->register_resource(engine, &resourceDesc, &resourceHandle);
    std::cout << "resource registered: handle=" << resourceHandle << "\n";

    zpc_runtime_resource_info_v1_t resourceInfo{};
    resourceInfo.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_resource_info_v1_t));
    resourceExtension->query_resource_info(engine, resourceHandle, &resourceInfo);
    std::cout << "resource info: label=" << to_string(resourceInfo.resource_label)
              << " executor=" << to_string(resourceInfo.executor_name)
              << " dirty=" << resourceInfo.dirty
              << " stale=" << resourceInfo.stale
              << " leases=" << resourceInfo.lease_count << "\n";

    void *resourcePayload = nullptr;
    zpc_runtime_resource_lease_handle_t *resourceLease = nullptr;
    resourceExtension->acquire_resource(engine, resourceHandle, &resourcePayload, &resourceLease);
    std::cout << "resource lease: payload="
        << static_cast<DemoResourcePayload *>(resourcePayload)->value << "\n";
    resourceInfo.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_resource_info_v1_t));
    resourceExtension->query_resource_info(engine, resourceHandle, &resourceInfo);
    std::cout << "resource lease state: leases=" << resourceInfo.lease_count
              << " last_access_epoch=" << resourceInfo.last_access_epoch << "\n";
    resourceExtension->release_lease(resourceLease);

    resourceExtension->mark_dirty(engine, resourceHandle, 1);
    uint64_t resourceEpoch = 0;
    resourceExtension->advance_epoch(engine, 2, &resourceEpoch);
    resourceInfo.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_resource_info_v1_t));
    resourceExtension->query_resource_info(engine, resourceHandle, &resourceInfo);
    std::cout << "resource dirty state: dirty=" << resourceInfo.dirty
              << " stale=" << resourceInfo.stale
              << " epoch=" << resourceEpoch << "\n";

    zpc_runtime_resource_maintenance_request_v1_t resourceRequest{};
    resourceRequest.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_resource_maintenance_request_v1_t));
    resourceRequest.kind = static_cast<uint32_t>(AsyncResourceMaintenanceKind::refresh);
    resourceRequest.require_dirty = 1;
    resourceRequest.clear_dirty_on_success = 1;
    resourceRequest.label = zpc_runtime_make_string_view("demo-resource-maintenance");

    zpc_runtime_submission_handle_t *resourceSubmission = nullptr;
    uint32_t resourceDisposition = 0;
    resourceExtension->schedule_maintenance(
      engine, resourceHandle, &resourceRequest, &resourceSubmission, &resourceDisposition);
    std::cout << "resource maintenance disposition=" << resourceDisposition << "\n";
    zpc_runtime_host_event_t resourceEvent{};
    resourceEvent.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    engineTable->query_event(resourceSubmission, &resourceEvent);
    std::cout << "resource event: status=" << resourceEvent.status_code
        << " token=" << resourceEvent.native_signal_token << "\n";
    engineTable->release_submission(resourceSubmission);
    resourceInfo.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_resource_info_v1_t));
    resourceExtension->query_resource_info(engine, resourceHandle, &resourceInfo);
    std::cout << "resource post-maintenance: dirty=" << resourceInfo.dirty
              << " stale=" << resourceInfo.stale
              << " last_access_epoch=" << resourceInfo.last_access_epoch << "\n";

    uint64_t staleScheduled = 0;
    resourceExtension->mark_dirty(engine, resourceHandle, 1);
    resourceExtension->advance_epoch(engine, 2, &resourceEpoch);
    resourceExtension->sweep_stale(engine, &resourceRequest, &staleScheduled);
    std::cout << "resource stale sweep scheduled=" << staleScheduled << "\n";

    zpc_runtime_resource_maintenance_request_v1_t retireRequest = resourceRequest;
    retireRequest.retire_on_success = 1;
    retireRequest.require_dirty = 0;
    retireRequest.label = zpc_runtime_make_string_view("demo-resource-retire");
    zpc_runtime_submission_handle_t *retireSubmission = nullptr;
    resourceExtension->schedule_maintenance(engine, resourceHandle, &retireRequest, &retireSubmission,
                        nullptr);
    engineTable->release_submission(retireSubmission);

    uint64_t retiredCount = 0;
    resourceExtension->collect_retired(engine, &retiredCount);
    std::cout << "resource retired=" << retiredCount
        << " maintenance_calls=" << resourceState.maintenanceCount
        << " destroy_calls=" << resourceState.destroyCount << "\n";

  DemoNativeQueueState nativeState{};
  zpc_runtime_native_queue_desc_t nativeDesc{};
  nativeDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_native_queue_desc_t));
  nativeDesc.backend_code = static_cast<uint32_t>(AsyncBackend::cuda);
  nativeDesc.queue_code = static_cast<uint32_t>(AsyncQueueClass::compute);
  nativeDesc.device = 0;
  nativeDesc.stream_or_queue_id = 2;
  nativeDesc.capability_mask = async_native_capability_submit | async_native_capability_sync
                             | async_native_capability_record | async_native_capability_wait;
  nativeDesc.sync_after_submit = 1;
  nativeDesc.record_after_submit = 1;

  zpc_runtime_native_queue_payload_t nativePayload{};
  nativePayload.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_native_queue_payload_t));
  nativePayload.binding = &nativeState;
  nativePayload.queue_handle = &demo_queue_handle;
  nativePayload.signal_handle = &demo_signal_handle;
  nativePayload.sync = &demo_sync;
  nativePayload.record = &demo_record;
  nativePayload.wait = &demo_wait;

  DemoTaskState nativeTaskState{};
  zpc_runtime_host_submit_payload_t nativeHostPayload{};
  nativeHostPayload.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
  nativeHostPayload.task = &demo_task;
  nativeHostPayload.user_data = &nativeTaskState;

  zpc_runtime_submission_desc_t nativeSubmitDesc = submitDesc;
  nativeSubmitDesc.task_label = zpc_runtime_make_string_view("demo-native-task");
  nativeSubmitDesc.backend_code = static_cast<uint32_t>(AsyncBackend::cuda);
  nativeSubmitDesc.reserved[zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&nativeHostPayload);

  zpc_runtime_extension_desc_t nativeExtensionDesc{};
  nativeExtensionDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
  engineTable->query_extension(engine,
                               zpc_runtime_make_string_view(zpc_runtime_native_queue_extension_name),
                               &nativeExtensionDesc);
    std::cout << "extension: " << to_string(nativeExtensionDesc.extension_name)
        << " version=" << nativeExtensionDesc.extension_version_major
        << "." << nativeExtensionDesc.extension_version_minor << "\n";
  const auto *nativeExtension =
      static_cast<const zpc_runtime_native_queue_extension_v1_t *>(nativeExtensionDesc.function_table);

    zpc_runtime_dependency_token_v1_t nativeDependencyTokens[2]{};
    nativeDependencyTokens[0].token = hostEvent.native_signal_token;
    nativeDependencyTokens[0].kind = ZPC_RUNTIME_DEPENDENCY_SUBMISSION_EVENT;
    nativeDependencyTokens[1].token = 0x1234u;
    nativeDependencyTokens[1].kind = ZPC_RUNTIME_DEPENDENCY_NATIVE_SIGNAL;
    zpc_runtime_dependency_list_v1_t nativeDependencyList{};
    nativeDependencyList.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_dependency_list_v1_t));
    nativeDependencyList.items = nativeDependencyTokens;
    nativeDependencyList.count = 2;
    nativeSubmitDesc.reserved[zpc_runtime_dependency_list_payload_slot] =
      reinterpret_cast<uint64_t>(&nativeDependencyList);

  zpc_runtime_submission_handle_t *nativeSubmission = nullptr;
  nativeExtension->submit(engine, &nativeDesc, &nativePayload, &nativeSubmitDesc, &nativeSubmission);

  zpc_runtime_host_event_t nativeEvent{};
  nativeEvent.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
  engineTable->query_event(nativeSubmission, &nativeEvent);
  nativeExtension->wait(&nativePayload, nativeEvent.native_signal_token);
  std::cout << "native event: status=" << nativeEvent.status_code
            << " signal=" << nativeEvent.native_signal_token
            << " syncs=" << nativeState.syncCount
            << " records=" << nativeState.recordCount
            << " waits=" << nativeState.waitCount
            << " first_wait="
            << static_cast<uint64_t>(reinterpret_cast<uintptr_t>(nativeState.waitedSignals.empty()
                                                                   ? nullptr
                                                                   : nativeState.waitedSignals.front()))
            << " last_wait="
            << static_cast<uint64_t>(reinterpret_cast<uintptr_t>(nativeState.waitedSignals.empty()
                                                                   ? nullptr
                                                                   : nativeState.waitedSignals.back()))
            << "\n";
  engineTable->release_submission(nativeSubmission);

  destroy_async_runtime_abi_engine(engine);
  return 0;
}