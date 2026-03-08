#include <iostream>
#include <string>

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

  int32_t demo_wait(void *binding, void *) {
    ++static_cast<DemoNativeQueueState *>(binding)->waitCount;
    return 1;
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
  std::cout << "extension: " << to_string(hostExtensionDesc.extension_name) << "\n";

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

  zpc_runtime_validation_summary_v1_t summary{};
  summary.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_validation_summary_v1_t));
  validationExtension->query_summary(engine, &summary);
  std::cout << "validation summary: suite=" << to_string(summary.suite)
            << " total=" << summary.total
            << " passed=" << summary.passed << "\n";

  zpc_runtime_string_view_t jsonView{};
  validationExtension->query_json(engine, &jsonView);
  std::cout << "validation json: " << to_string(jsonView) << "\n";

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
  const auto *nativeExtension =
      static_cast<const zpc_runtime_native_queue_extension_v1_t *>(nativeExtensionDesc.function_table);

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
            << " waits=" << nativeState.waitCount << "\n";
  engineTable->release_submission(nativeSubmission);

  destroy_async_runtime_abi_engine(engine);
  return 0;
}