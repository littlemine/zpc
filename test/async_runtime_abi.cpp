#include <cassert>
#include <vector>

#include "zensim/execution/AsyncRuntimeAbi.hpp"

namespace {

  struct CountingTaskState {
    int callCount{0};
    uint64_t lastSubmissionId{0};
  };

  struct SuspendedTaskState {
    int callCount{0};
    bool sawStop{false};
  };

  struct FakeNativeQueueState {
    void *queueHandle{reinterpret_cast<void *>(static_cast<uintptr_t>(0xAAAA))};
    void *signalHandle{reinterpret_cast<void *>(static_cast<uintptr_t>(0xBBBB))};
    int syncCount{0};
    int recordCount{0};
    int waitCount{0};
    void *lastForeignSignal{nullptr};
    std::vector<void *> waitedSignals{};
  };

  zpc_runtime_host_task_result_e counting_task(void *user_data,
                                               const zpc_runtime_host_task_context_t *context,
                                               const zpc_runtime_submission_desc_t *desc) {
    auto *state = static_cast<CountingTaskState *>(user_data);
    assert(state != nullptr);
    assert(context != nullptr);
    assert(desc != nullptr);
    ++state->callCount;
    state->lastSubmissionId = context->submission_id;
    assert(desc->priority == 7);
    return ZPC_RUNTIME_HOST_TASK_COMPLETED;
  }

  zpc_runtime_host_task_result_e suspending_task(void *user_data,
                                                 const zpc_runtime_host_task_context_t *context,
                                                 const zpc_runtime_submission_desc_t *) {
    auto *state = static_cast<SuspendedTaskState *>(user_data);
    assert(state != nullptr);
    assert(context != nullptr);
    ++state->callCount;
    if (context->stop_requested) {
      state->sawStop = true;
      return ZPC_RUNTIME_HOST_TASK_CANCELLED;
    }
    return ZPC_RUNTIME_HOST_TASK_SUSPEND;
  }

  std::string string_from_view(zpc_runtime_string_view_t view) {
    if (!view.data || view.size == 0) return {};
    return std::string{view.data, view.size};
  }

  void *fake_native_queue_handle(void *binding) {
    return static_cast<FakeNativeQueueState *>(binding)->queueHandle;
  }

  void *fake_native_signal_handle(void *binding) {
    return static_cast<FakeNativeQueueState *>(binding)->signalHandle;
  }

  int32_t fake_native_sync(void *binding) {
    ++static_cast<FakeNativeQueueState *>(binding)->syncCount;
    return 1;
  }

  int32_t fake_native_record(void *binding) {
    ++static_cast<FakeNativeQueueState *>(binding)->recordCount;
    return 1;
  }

  int32_t fake_native_wait(void *binding, void *foreignSignal) {
    auto *state = static_cast<FakeNativeQueueState *>(binding);
    ++state->waitCount;
    state->lastForeignSignal = foreignSignal;
    state->waitedSignals.push_back(foreignSignal);
    return 1;
  }

}  // namespace

int main() {
  zpc_runtime_abi_header_t header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_v1_t));
  assert(header.abi_major == ZPC_RUNTIME_ABI_VERSION_MAJOR);
  assert(header.abi_minor == ZPC_RUNTIME_ABI_VERSION_MINOR);
  assert(header.size == sizeof(zpc_runtime_engine_v1_t));
  assert(zpc_runtime_is_abi_compatible(&header, (uint32_t)sizeof(zpc_runtime_abi_header_t))
         == ZPC_RUNTIME_ABI_OK);

  zpc_runtime_abi_header_t smaller = header;
  smaller.size = 4;
  assert(zpc_runtime_is_abi_compatible(&smaller, (uint32_t)sizeof(zpc_runtime_engine_v1_t))
         == ZPC_RUNTIME_ABI_INSUFFICIENT_SIZE);

  zpc_runtime_abi_header_t wrongMajor = header;
  wrongMajor.abi_major = 9;
  assert(zpc_runtime_is_abi_compatible(&wrongMajor, (uint32_t)sizeof(zpc_runtime_abi_header_t))
         == ZPC_RUNTIME_ABI_INCOMPATIBLE_MAJOR);

    zs::AsyncRuntimeAbiEngineConfig config{};
    config.engineName = "zpc-test-engine";
    config.buildId = "local";
    auto *engine = zs::make_async_runtime_abi_engine(config);
    assert(engine != nullptr);

    const auto *engineTable = zs::async_runtime_abi_engine_table(engine);
    assert(engineTable != nullptr);
    assert(zpc_runtime_check_engine_table(engineTable) == ZPC_RUNTIME_ABI_OK);

    zpc_runtime_engine_desc_t engineDesc{};
    engineDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_desc_t));
    assert(engineTable->query_engine_desc(engine, &engineDesc) == ZPC_RUNTIME_ABI_OK);
  assert(engineDesc.engine_name.size == 15);
  assert(engineDesc.capability_mask & ZPC_RUNTIME_ABI_CAP_HOT_UPGRADE);
    assert(zs::zpc_runtime_string_view_equals(engineDesc.build_id, "local"));

  zpc_runtime_extension_desc_t extensionDesc{};
    extensionDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
    assert(engineTable->query_extension(
         engine, zs::zpc_runtime_make_string_view(zs::zpc_runtime_host_submit_extension_name),
         &extensionDesc)
       == ZPC_RUNTIME_ABI_OK);
  assert(extensionDesc.extension_version_major == 1);
  assert(extensionDesc.extension_version_minor == 0);
    const auto *hostExtension =
      static_cast<const zpc_runtime_host_submit_extension_v1_t *>(extensionDesc.function_table);
    assert(hostExtension != nullptr);
    assert(hostExtension->payload_reserved_index == zs::zpc_runtime_host_submit_payload_slot);

    zpc_runtime_extension_desc_t nativeExtensionDesc{};
    nativeExtensionDesc.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
    assert(engineTable->query_extension(
         engine, zs::zpc_runtime_make_string_view(zs::zpc_runtime_native_queue_extension_name),
         &nativeExtensionDesc)
       == ZPC_RUNTIME_ABI_OK);
    assert(nativeExtensionDesc.extension_version_minor == 1);
    const auto *nativeExtension =
      static_cast<const zpc_runtime_native_queue_extension_v1_t *>(nativeExtensionDesc.function_table);
    assert(nativeExtension != nullptr);

    zpc_runtime_extension_desc_t validationExtensionDesc{};
    validationExtensionDesc.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
    assert(engineTable->query_extension(
         engine, zs::zpc_runtime_make_string_view(zs::zpc_runtime_validation_extension_name),
         &validationExtensionDesc)
       == ZPC_RUNTIME_ABI_OK);
    const auto *validationExtension =
      static_cast<const zpc_runtime_validation_extension_v1_t *>(validationExtensionDesc.function_table);
    assert(validationExtension != nullptr);

    zpc_runtime_validation_summary_v1_t emptySummary{};
    emptySummary.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_validation_summary_v1_t));
    assert(validationExtension->query_summary(engine, &emptySummary)
           == ZPC_RUNTIME_ABI_UNSUPPORTED_OPERATION);

    CountingTaskState countingState{};
    zpc_runtime_host_submit_payload_t payload{};
    payload.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
    payload.task = &counting_task;
    payload.user_data = &countingState;

    zpc_runtime_submission_desc_t submitDesc{};
    submitDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_submission_desc_t));
    submitDesc.executor_name = zs::zpc_runtime_make_string_view("inline");
    submitDesc.task_label = zs::zpc_runtime_make_string_view("abi-counting-task");
    submitDesc.domain_code = static_cast<uint32_t>(zs::AsyncDomain::compute);
    submitDesc.queue_code = static_cast<uint32_t>(zs::AsyncQueueClass::compute);
    submitDesc.backend_code = static_cast<uint32_t>(zs::AsyncBackend::inline_host);
    submitDesc.priority = 7;
    submitDesc.reserved[zs::zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&payload);

    zpc_runtime_submission_handle_t *submission = nullptr;
    assert(engineTable->submit(engine, &submitDesc, &submission) == ZPC_RUNTIME_ABI_OK);
    assert(submission != nullptr);
    assert(countingState.callCount == 1);
    assert(countingState.lastSubmissionId != 0);

    zpc_runtime_host_event_t eventDesc{};
    eventDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    assert(engineTable->query_event(submission, &eventDesc) == ZPC_RUNTIME_ABI_OK);
    assert(eventDesc.status_code == static_cast<uint64_t>(zs::AsyncTaskStatus::completed));
    assert(eventDesc.native_signal_token == countingState.lastSubmissionId);
    assert(engineTable->release_submission(submission) == ZPC_RUNTIME_ABI_OK);

    zpc_runtime_dependency_token_v1_t dependentHostToken{};
    dependentHostToken.token = eventDesc.native_signal_token;
    dependentHostToken.kind = ZPC_RUNTIME_DEPENDENCY_SUBMISSION_EVENT;
    zpc_runtime_dependency_list_v1_t dependentHostList{};
    dependentHostList.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_dependency_list_v1_t));
    dependentHostList.items = &dependentHostToken;
    dependentHostList.count = 1;

    CountingTaskState dependentHostState{};
    zpc_runtime_host_submit_payload_t dependentHostPayload{};
    dependentHostPayload.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
    dependentHostPayload.task = &counting_task;
    dependentHostPayload.user_data = &dependentHostState;

    zpc_runtime_submission_desc_t dependentHostDesc = submitDesc;
    dependentHostDesc.task_label = zs::zpc_runtime_make_string_view("abi-dependent-host-task");
    dependentHostDesc.reserved[zs::zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&dependentHostPayload);
    dependentHostDesc.reserved[zs::zpc_runtime_dependency_list_payload_slot] =
      reinterpret_cast<uint64_t>(&dependentHostList);

    zpc_runtime_submission_handle_t *dependentHostSubmission = nullptr;
    assert(engineTable->submit(engine, &dependentHostDesc, &dependentHostSubmission)
           == ZPC_RUNTIME_ABI_OK);
    assert(dependentHostSubmission != nullptr);
    assert(dependentHostState.callCount == 1);
    assert(engineTable->release_submission(dependentHostSubmission) == ZPC_RUNTIME_ABI_OK);

    zpc_runtime_dependency_token_v1_t invalidHostToken{};
    invalidHostToken.token = 0x1234u;
    invalidHostToken.kind = ZPC_RUNTIME_DEPENDENCY_NATIVE_SIGNAL;
    zpc_runtime_dependency_list_v1_t invalidHostList{};
    invalidHostList.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_dependency_list_v1_t));
    invalidHostList.items = &invalidHostToken;
    invalidHostList.count = 1;

    zpc_runtime_submission_desc_t invalidHostDesc = submitDesc;
    invalidHostDesc.task_label = zs::zpc_runtime_make_string_view("abi-invalid-host-dependency");
    invalidHostDesc.reserved[zs::zpc_runtime_dependency_list_payload_slot] =
      reinterpret_cast<uint64_t>(&invalidHostList);
    zpc_runtime_submission_handle_t *invalidHostSubmission = nullptr;
        assert(engineTable->submit(engine, &invalidHostDesc, &invalidHostSubmission)
          == ZPC_RUNTIME_ABI_UNSUPPORTED_OPERATION);
        assert(invalidHostSubmission == nullptr);

    FakeNativeQueueState nativeState{};
    zpc_runtime_native_queue_desc_t nativeDesc{};
    nativeDesc.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_native_queue_desc_t));
    nativeDesc.backend_code = static_cast<uint32_t>(zs::AsyncBackend::cuda);
    nativeDesc.queue_code = static_cast<uint32_t>(zs::AsyncQueueClass::compute);
    nativeDesc.device = 2;
    nativeDesc.stream_or_queue_id = 5;
    nativeDesc.capability_mask = zs::async_native_capability_submit
                   | zs::async_native_capability_sync
                   | zs::async_native_capability_record
                   | zs::async_native_capability_wait;
    nativeDesc.sync_after_submit = 1;
    nativeDesc.record_after_submit = 1;

    zpc_runtime_native_queue_payload_t nativePayload{};
    nativePayload.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_native_queue_payload_t));
    nativePayload.binding = &nativeState;
    nativePayload.queue_handle = &fake_native_queue_handle;
    nativePayload.signal_handle = &fake_native_signal_handle;
    nativePayload.sync = &fake_native_sync;
    nativePayload.record = &fake_native_record;
    nativePayload.wait = &fake_native_wait;

    CountingTaskState nativeTaskState{};
    zpc_runtime_host_submit_payload_t nativeHostPayload{};
    nativeHostPayload.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
    nativeHostPayload.task = &counting_task;
    nativeHostPayload.user_data = &nativeTaskState;

    zpc_runtime_submission_desc_t nativeSubmitDesc = submitDesc;
    nativeSubmitDesc.task_label = zs::zpc_runtime_make_string_view("abi-native-task");
    nativeSubmitDesc.backend_code = static_cast<uint32_t>(zs::AsyncBackend::cuda);
    nativeSubmitDesc.queue_code = static_cast<uint32_t>(zs::AsyncQueueClass::compute);
    nativeSubmitDesc.reserved[zs::zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&nativeHostPayload);

    zpc_runtime_dependency_token_v1_t nativeDependencyTokens[2]{};
    nativeDependencyTokens[0].token = eventDesc.native_signal_token;
    nativeDependencyTokens[0].kind = ZPC_RUNTIME_DEPENDENCY_SUBMISSION_EVENT;
    nativeDependencyTokens[1].token = 0x1234u;
    nativeDependencyTokens[1].kind = ZPC_RUNTIME_DEPENDENCY_NATIVE_SIGNAL;
    zpc_runtime_dependency_list_v1_t nativeDependencyList{};
    nativeDependencyList.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_dependency_list_v1_t));
    nativeDependencyList.items = nativeDependencyTokens;
    nativeDependencyList.count = 2;
    nativeSubmitDesc.reserved[zs::zpc_runtime_dependency_list_payload_slot] =
      reinterpret_cast<uint64_t>(&nativeDependencyList);

    zpc_runtime_submission_handle_t *nativeSubmission = nullptr;
    assert(nativeExtension->submit(engine, &nativeDesc, &nativePayload, &nativeSubmitDesc,
                                   &nativeSubmission)
           == ZPC_RUNTIME_ABI_OK);
    assert(nativeSubmission != nullptr);
    assert(nativeTaskState.callCount == 1);
    assert(nativeState.recordCount == 1);
    assert(nativeState.syncCount == 1);

    zpc_runtime_host_event_t nativeEvent{};
    nativeEvent.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    assert(engineTable->query_event(nativeSubmission, &nativeEvent) == ZPC_RUNTIME_ABI_OK);
    assert(nativeEvent.status_code == static_cast<uint64_t>(zs::AsyncTaskStatus::completed));
    assert(nativeEvent.native_signal_token
           == static_cast<uint64_t>(reinterpret_cast<uintptr_t>(nativeState.signalHandle)));
    assert(nativeState.waitCount == 1);
    assert(nativeState.waitedSignals.size() == 1);
    assert(nativeState.waitedSignals[0] == reinterpret_cast<void *>(static_cast<uintptr_t>(0x1234u)));
    assert(nativeExtension->wait(&nativePayload, nativeEvent.native_signal_token)
           == ZPC_RUNTIME_ABI_OK);
    assert(nativeState.waitCount == 2);
    assert(nativeState.lastForeignSignal == nativeState.signalHandle);
    assert(nativeState.waitedSignals.size() == 2);
    assert(nativeState.waitedSignals[1] == nativeState.signalHandle);
    assert(engineTable->release_submission(nativeSubmission) == ZPC_RUNTIME_ABI_OK);

    SuspendedTaskState suspendedState{};
    zpc_runtime_host_submit_payload_t suspendedPayload{};
    suspendedPayload.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
    suspendedPayload.task = &suspending_task;
    suspendedPayload.user_data = &suspendedState;

    zpc_runtime_submission_desc_t suspendedDesc = submitDesc;
    suspendedDesc.task_label = zs::zpc_runtime_make_string_view("abi-suspended-task");
    suspendedDesc.reserved[zs::zpc_runtime_host_submit_payload_slot] =
      reinterpret_cast<uint64_t>(&suspendedPayload);

    zpc_runtime_submission_handle_t *suspendedSubmission = nullptr;
    assert(engineTable->submit(engine, &suspendedDesc, &suspendedSubmission) == ZPC_RUNTIME_ABI_OK);
    assert(suspendedSubmission != nullptr);
    assert(suspendedState.callCount == 1);

    zpc_runtime_host_event_t suspendedEvent{};
    suspendedEvent.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    assert(engineTable->query_event(suspendedSubmission, &suspendedEvent) == ZPC_RUNTIME_ABI_OK);
    assert(suspendedEvent.status_code == static_cast<uint64_t>(zs::AsyncTaskStatus::suspended));

    assert(engineTable->cancel(suspendedSubmission) == ZPC_RUNTIME_ABI_OK);
    suspendedEvent.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    assert(engineTable->query_event(suspendedSubmission, &suspendedEvent) == ZPC_RUNTIME_ABI_OK);
    assert(suspendedEvent.status_code == static_cast<uint64_t>(zs::AsyncTaskStatus::cancelled));
    assert(suspendedState.callCount == 2);
    assert(suspendedState.sawStop);
    assert(engineTable->release_submission(suspendedSubmission) == ZPC_RUNTIME_ABI_OK);

    zs::ValidationSuiteReport report{};
    report.suite = "async-abi";
    zs::ValidationRecord record{};
    record.recordId = "abi.validation.sample";
    record.suite = "async-abi";
    record.name = "host-export";
    record.backend = "inline_host";
    record.executor = "inline";
    record.target = "cpu";
    record.kind = zs::ValidationRecordKind::validation;
    record.outcome = zs::ValidationOutcome::pass;
    record.durationNs = 33;
    record.measurements.push_back(zs::ValidationMeasurement{
      "latency", "ns", 33.0,
      zs::ValidationThreshold{zs::ValidationThresholdMode::less_equal, 64.0, 0.0}});
    report.records.push_back(record);
    zs::publish_async_runtime_validation_report(engine, report);

    zpc_runtime_validation_summary_v1_t validationSummary{};
    validationSummary.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_validation_summary_v1_t));
    assert(validationExtension->query_summary(engine, &validationSummary) == ZPC_RUNTIME_ABI_OK);
    assert(string_from_view(validationSummary.schema_version) == "zpc.validation.v1");
    assert(string_from_view(validationSummary.suite) == "async-abi");
    assert(validationSummary.total == 1);
    assert(validationSummary.passed == 1);
    assert(validationSummary.failed == 0);

    zpc_runtime_string_view_t jsonView{};
    assert(validationExtension->query_json(engine, &jsonView) == ZPC_RUNTIME_ABI_OK);
    const auto json = string_from_view(jsonView);
    assert(json.find("\"schemaVersion\":\"zpc.validation.v1\"") != std::string::npos);
    assert(json.find("\"recordId\":\"abi.validation.sample\"") != std::string::npos);

    zpc_runtime_string_view_t textView{};
    assert(validationExtension->query_text(engine, &textView) == ZPC_RUNTIME_ABI_OK);
    const auto text = string_from_view(textView);
    assert(text.find("suite=async-abi") != std::string::npos);
    assert(text.find("host-export") != std::string::npos);

    zs::clear_async_runtime_validation_report(engine);
    validationSummary.header =
      zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_validation_summary_v1_t));
    assert(validationExtension->query_summary(engine, &validationSummary)
           == ZPC_RUNTIME_ABI_UNSUPPORTED_OPERATION);

    zs::destroy_async_runtime_abi_engine(engine);

  return 0;
}