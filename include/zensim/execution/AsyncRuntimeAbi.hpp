#pragma once

#include <stddef.h>
#include <stdint.h>

#include "zensim/Platform.hpp"

#ifdef __cplusplus
extern "C" {
#endif

  enum {
    ZPC_RUNTIME_ABI_VERSION_MAJOR = 1u,
    ZPC_RUNTIME_ABI_VERSION_MINOR = 0u,
    ZPC_RUNTIME_ABI_VERSION_PATCH = 0u
  };

  enum {
    ZPC_RUNTIME_ABI_CAP_ASYNC_SUBMIT = 1u << 0,
    ZPC_RUNTIME_ABI_CAP_NATIVE_QUEUE = 1u << 1,
    ZPC_RUNTIME_ABI_CAP_VALIDATION = 1u << 2,
    ZPC_RUNTIME_ABI_CAP_REFLECTION = 1u << 3,
    ZPC_RUNTIME_ABI_CAP_PYTHON_EXPORT = 1u << 4,
    ZPC_RUNTIME_ABI_CAP_HOT_UPGRADE = 1u << 5
  };

  enum zpc_runtime_abi_result_e {
    ZPC_RUNTIME_ABI_OK = 0,
    ZPC_RUNTIME_ABI_ERROR = -1,
    ZPC_RUNTIME_ABI_INCOMPATIBLE_MAJOR = -2,
    ZPC_RUNTIME_ABI_INSUFFICIENT_SIZE = -3,
    ZPC_RUNTIME_ABI_UNSUPPORTED_OPERATION = -4
  };

  typedef struct zpc_runtime_engine_handle_t zpc_runtime_engine_handle_t;
  typedef struct zpc_runtime_submission_handle_t zpc_runtime_submission_handle_t;
  typedef struct zpc_runtime_extension_handle_t zpc_runtime_extension_handle_t;

  typedef struct zpc_runtime_abi_header_t {
    uint32_t size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint32_t reserved0;
  } zpc_runtime_abi_header_t;

  typedef struct zpc_runtime_string_view_t {
    const char *data;
    size_t size;
  } zpc_runtime_string_view_t;

  typedef struct zpc_runtime_engine_desc_t {
    zpc_runtime_abi_header_t header;
    zpc_runtime_string_view_t engine_name;
    zpc_runtime_string_view_t build_id;
    uint64_t capability_mask;
    uint64_t reserved[4];
  } zpc_runtime_engine_desc_t;

  typedef struct zpc_runtime_submission_desc_t {
    zpc_runtime_abi_header_t header;
    zpc_runtime_string_view_t executor_name;
    zpc_runtime_string_view_t task_label;
    uint32_t domain_code;
    uint32_t queue_code;
    uint32_t backend_code;
    uint32_t priority;
    uint64_t reserved[4];
  } zpc_runtime_submission_desc_t;

  typedef struct zpc_runtime_host_event_t {
    zpc_runtime_abi_header_t header;
    uint64_t status_code;
    uint64_t native_signal_token;
    uint64_t reserved[4];
  } zpc_runtime_host_event_t;

  typedef struct zpc_runtime_extension_desc_t {
    zpc_runtime_abi_header_t header;
    zpc_runtime_string_view_t extension_name;
    uint32_t extension_version_major;
    uint32_t extension_version_minor;
    const void *function_table;
    uint64_t reserved[4];
  } zpc_runtime_extension_desc_t;

  typedef struct zpc_runtime_host_task_context_t {
    zpc_runtime_abi_header_t header;
    uint64_t submission_id;
    uint64_t stop_requested;
    uint64_t interrupt_requested;
    uint64_t reserved[4];
  } zpc_runtime_host_task_context_t;

  typedef enum zpc_runtime_host_task_result_e {
    ZPC_RUNTIME_HOST_TASK_SUSPEND = 0,
    ZPC_RUNTIME_HOST_TASK_COMPLETED = 1,
    ZPC_RUNTIME_HOST_TASK_CANCELLED = 2,
    ZPC_RUNTIME_HOST_TASK_FAILED = 3
  } zpc_runtime_host_task_result_e;

  typedef zpc_runtime_host_task_result_e (*zpc_runtime_host_task_fn)(
      void *user_data, const zpc_runtime_host_task_context_t *context,
      const zpc_runtime_submission_desc_t *desc);

  typedef struct zpc_runtime_host_submit_payload_t {
    zpc_runtime_abi_header_t header;
    zpc_runtime_host_task_fn task;
    void *user_data;
    uint64_t reserved[4];
  } zpc_runtime_host_submit_payload_t;

  typedef struct zpc_runtime_host_submit_extension_v1_t {
    zpc_runtime_abi_header_t header;
    uint32_t payload_reserved_index;
    uint32_t reserved0;
    uint64_t reserved[6];
  } zpc_runtime_host_submit_extension_v1_t;

  typedef int32_t (*zpc_runtime_query_engine_desc_fn)(zpc_runtime_engine_handle_t *engine,
                                                      zpc_runtime_engine_desc_t *desc);
  typedef int32_t (*zpc_runtime_submit_fn)(zpc_runtime_engine_handle_t *engine,
                                           const zpc_runtime_submission_desc_t *desc,
                                           zpc_runtime_submission_handle_t **submission);
  typedef int32_t (*zpc_runtime_query_event_fn)(zpc_runtime_submission_handle_t *submission,
                                                zpc_runtime_host_event_t *event_desc);
  typedef int32_t (*zpc_runtime_cancel_fn)(zpc_runtime_submission_handle_t *submission);
  typedef int32_t (*zpc_runtime_release_submission_fn)(zpc_runtime_submission_handle_t *submission);
  typedef int32_t (*zpc_runtime_query_extension_fn)(zpc_runtime_engine_handle_t *engine,
                                                    zpc_runtime_string_view_t extension_name,
                                                    zpc_runtime_extension_desc_t *extension_desc);

  typedef struct zpc_runtime_engine_v1_t {
    zpc_runtime_abi_header_t header;
    zpc_runtime_query_engine_desc_fn query_engine_desc;
    zpc_runtime_submit_fn submit;
    zpc_runtime_query_event_fn query_event;
    zpc_runtime_cancel_fn cancel;
    zpc_runtime_release_submission_fn release_submission;
    zpc_runtime_query_extension_fn query_extension;
    void *reserved[8];
  } zpc_runtime_engine_v1_t;

  typedef struct zpc_runtime_plugin_manifest_t {
    zpc_runtime_abi_header_t header;
    zpc_runtime_string_view_t plugin_name;
    zpc_runtime_string_view_t plugin_version;
    uint64_t required_capability_mask;
    uint64_t optional_capability_mask;
    uint64_t reserved[4];
  } zpc_runtime_plugin_manifest_t;

  static inline zpc_runtime_abi_header_t zpc_runtime_make_header(uint32_t size) {
    zpc_runtime_abi_header_t header;
    header.size = size;
    header.abi_major = (uint16_t)ZPC_RUNTIME_ABI_VERSION_MAJOR;
    header.abi_minor = (uint16_t)ZPC_RUNTIME_ABI_VERSION_MINOR;
    header.flags = 0;
    header.reserved0 = 0;
    return header;
  }

  static inline int32_t zpc_runtime_is_abi_compatible(const zpc_runtime_abi_header_t *header,
                                                      uint32_t expected_size) {
    if (!header) return ZPC_RUNTIME_ABI_ERROR;
    if (header->abi_major != ZPC_RUNTIME_ABI_VERSION_MAJOR)
      return ZPC_RUNTIME_ABI_INCOMPATIBLE_MAJOR;
    if (header->size < expected_size) return ZPC_RUNTIME_ABI_INSUFFICIENT_SIZE;
    return ZPC_RUNTIME_ABI_OK;
  }

  static inline int32_t zpc_runtime_check_engine_table(const zpc_runtime_engine_v1_t *table) {
    int32_t compatibility = zpc_runtime_is_abi_compatible(
        table ? &table->header : (const zpc_runtime_abi_header_t *)0,
        (uint32_t)sizeof(zpc_runtime_engine_v1_t));
    if (compatibility != ZPC_RUNTIME_ABI_OK) return compatibility;
    if (!table->query_engine_desc || !table->submit || !table->query_event
        || !table->cancel || !table->release_submission)
      return ZPC_RUNTIME_ABI_ERROR;
    return ZPC_RUNTIME_ABI_OK;
  }

#ifdef __cplusplus
}

#include <memory>

namespace zs {

#include "zensim/execution/AsyncRuntime.hpp"

  inline constexpr const char *zpc_runtime_host_submit_extension_name =
      "zpc.runtime.host_submit.v1";
  inline constexpr uint32_t zpc_runtime_host_submit_payload_slot = 0u;

  inline zpc_runtime_string_view_t zpc_runtime_make_string_view(const char *text) noexcept {
    zpc_runtime_string_view_t view{};
    if (!text) return view;
    view.data = text;
    while (text[view.size]) ++view.size;
    return view;
  }

  inline SmallString zpc_runtime_small_string_from_view(zpc_runtime_string_view_t view) noexcept {
    SmallString value{};
    constexpr size_t capacity = SmallString::nbytes;
    if (!view.data || view.size == 0) return value;
    const size_t count = view.size < capacity ? view.size : capacity - 1;
    for (size_t i = 0; i != count; ++i) value.buf[i] = view.data[i];
    value.buf[count] = '\0';
    return value;
  }

  inline bool zpc_runtime_string_view_equals(zpc_runtime_string_view_t view,
                                             const char *text) noexcept {
    if (!text) return !view.data || view.size == 0;
    size_t index = 0;
    for (; index != view.size && text[index]; ++index)
      if (view.data[index] != text[index]) return false;
    return index == view.size && text[index] == '\0';
  }

  inline AsyncDomain async_domain_from_abi_code(uint32_t code) noexcept {
    return static_cast<AsyncDomain>(static_cast<u8>(code));
  }

  inline AsyncQueueClass async_queue_from_abi_code(uint32_t code) noexcept {
    return static_cast<AsyncQueueClass>(static_cast<u8>(code));
  }

  inline AsyncBackend async_backend_from_abi_code(uint32_t code) noexcept {
    return static_cast<AsyncBackend>(static_cast<u8>(code));
  }

  inline AsyncPollStatus async_poll_status_from_host_task_result(
      zpc_runtime_host_task_result_e result) noexcept {
    switch (result) {
      case ZPC_RUNTIME_HOST_TASK_SUSPEND:
        return AsyncPollStatus::suspend;
      case ZPC_RUNTIME_HOST_TASK_COMPLETED:
        return AsyncPollStatus::completed;
      case ZPC_RUNTIME_HOST_TASK_CANCELLED:
        return AsyncPollStatus::cancelled;
      default:
        return AsyncPollStatus::failed;
    }
  }

  struct AsyncRuntimeAbiEngineConfig {
    SmallString engineName{"zpc-async-runtime"};
    SmallString buildId{"local"};
    uint64_t capabilityMask{ZPC_RUNTIME_ABI_CAP_ASYNC_SUBMIT | ZPC_RUNTIME_ABI_CAP_HOT_UPGRADE};
    size_t workerCount{1};
  };

  struct AsyncRuntimeAbiSubmissionState {
    zpc_runtime_submission_desc_t desc{};
    SmallString executorName{};
    SmallString taskLabel{};
    zpc_runtime_host_task_fn task{};
    void *userData{nullptr};
    AsyncStopSource cancellation{};

    void refresh_views() noexcept {
      desc.executor_name = zpc_runtime_make_string_view(executorName.asChars());
      desc.task_label = zpc_runtime_make_string_view(taskLabel.asChars());
    }
  };

}  // namespace zs

struct zpc_runtime_engine_handle_t {
  std::unique_ptr<zs::AsyncRuntime> runtime{};
  zs::SmallString engine_name{"zpc-async-runtime"};
  zs::SmallString build_id{"local"};
  uint64_t capability_mask{ZPC_RUNTIME_ABI_CAP_ASYNC_SUBMIT | ZPC_RUNTIME_ABI_CAP_HOT_UPGRADE};
  zpc_runtime_host_submit_extension_v1_t host_submit_extension{};
  zpc_runtime_engine_v1_t table{};
};

struct zpc_runtime_submission_handle_t {
  zpc_runtime_engine_handle_t *engine{nullptr};
  zs::AsyncSubmissionHandle handle{};
  std::shared_ptr<zs::AsyncRuntimeAbiSubmissionState> state{};
};

namespace zs {

  inline int32_t zpc_runtime_async_query_engine_desc(zpc_runtime_engine_handle_t *engine,
                                                     zpc_runtime_engine_desc_t *desc) {
    if (!engine || !desc) return ZPC_RUNTIME_ABI_ERROR;
    const int32_t compatibility =
        zpc_runtime_is_abi_compatible(&desc->header, (uint32_t)sizeof(zpc_runtime_engine_desc_t));
    if (compatibility != ZPC_RUNTIME_ABI_OK) return compatibility;
    desc->header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_desc_t));
    desc->engine_name = zpc_runtime_make_string_view(engine->engine_name.asChars());
    desc->build_id = zpc_runtime_make_string_view(engine->build_id.asChars());
    desc->capability_mask = engine->capability_mask;
    for (auto &slot : desc->reserved) slot = 0;
    return ZPC_RUNTIME_ABI_OK;
  }

  inline int32_t zpc_runtime_async_query_event(zpc_runtime_submission_handle_t *submission,
                                               zpc_runtime_host_event_t *event_desc) {
    if (!submission || !event_desc) return ZPC_RUNTIME_ABI_ERROR;
    const int32_t compatibility =
        zpc_runtime_is_abi_compatible(&event_desc->header, (uint32_t)sizeof(zpc_runtime_host_event_t));
    if (compatibility != ZPC_RUNTIME_ABI_OK) return compatibility;
    event_desc->header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    event_desc->status_code = static_cast<uint64_t>(submission->handle.status());
    event_desc->native_signal_token = submission->handle.id();
    for (auto &slot : event_desc->reserved) slot = 0;
    return ZPC_RUNTIME_ABI_OK;
  }

  inline int32_t zpc_runtime_async_cancel(zpc_runtime_submission_handle_t *submission) {
    if (!submission || !submission->state) return ZPC_RUNTIME_ABI_ERROR;
    submission->state->cancellation.request_stop();
    if (submission->engine && submission->engine->runtime
        && submission->handle.status() == AsyncTaskStatus::suspended)
      submission->engine->runtime->resume(submission->handle);
    return ZPC_RUNTIME_ABI_OK;
  }

  inline int32_t zpc_runtime_async_release_submission(zpc_runtime_submission_handle_t *submission) {
    delete submission;
    return ZPC_RUNTIME_ABI_OK;
  }

  inline int32_t zpc_runtime_async_query_extension(zpc_runtime_engine_handle_t *engine,
                                                   zpc_runtime_string_view_t extension_name,
                                                   zpc_runtime_extension_desc_t *extension_desc) {
    if (!engine || !extension_desc) return ZPC_RUNTIME_ABI_ERROR;
    const int32_t compatibility = zpc_runtime_is_abi_compatible(
        &extension_desc->header, (uint32_t)sizeof(zpc_runtime_extension_desc_t));
    if (compatibility != ZPC_RUNTIME_ABI_OK) return compatibility;
    if (!zpc_runtime_string_view_equals(extension_name, zpc_runtime_host_submit_extension_name))
      return ZPC_RUNTIME_ABI_UNSUPPORTED_OPERATION;

    extension_desc->header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
    extension_desc->extension_name =
        zpc_runtime_make_string_view(zpc_runtime_host_submit_extension_name);
    extension_desc->extension_version_major = 1;
    extension_desc->extension_version_minor = 0;
    extension_desc->function_table = &engine->host_submit_extension;
    for (auto &slot : extension_desc->reserved) slot = 0;
    return ZPC_RUNTIME_ABI_OK;
  }

  inline int32_t zpc_runtime_async_submit(zpc_runtime_engine_handle_t *engine,
                                          const zpc_runtime_submission_desc_t *desc,
                                          zpc_runtime_submission_handle_t **submission) {
    if (!engine || !engine->runtime || !desc || !submission) return ZPC_RUNTIME_ABI_ERROR;
    const int32_t compatibility =
        zpc_runtime_is_abi_compatible(&desc->header, (uint32_t)sizeof(zpc_runtime_submission_desc_t));
    if (compatibility != ZPC_RUNTIME_ABI_OK) return compatibility;

    const auto *payload =
        reinterpret_cast<const zpc_runtime_host_submit_payload_t *>(desc->reserved[zpc_runtime_host_submit_payload_slot]);
    if (!payload) return ZPC_RUNTIME_ABI_UNSUPPORTED_OPERATION;

    const int32_t payloadCompatibility = zpc_runtime_is_abi_compatible(
        &payload->header, (uint32_t)sizeof(zpc_runtime_host_submit_payload_t));
    if (payloadCompatibility != ZPC_RUNTIME_ABI_OK) return payloadCompatibility;
    if (!payload->task) return ZPC_RUNTIME_ABI_ERROR;

    auto *submission_handle = new zpc_runtime_submission_handle_t{};
    submission_handle->engine = engine;
    submission_handle->state = std::make_shared<AsyncRuntimeAbiSubmissionState>();
    submission_handle->state->desc = *desc;
    submission_handle->state->executorName = zpc_runtime_small_string_from_view(desc->executor_name);
    submission_handle->state->taskLabel = zpc_runtime_small_string_from_view(desc->task_label);
    submission_handle->state->task = payload->task;
    submission_handle->state->userData = payload->user_data;
    submission_handle->state->refresh_views();

    AsyncSubmission async_submission{};
    async_submission.executor = submission_handle->state->executorName.size()
                                    ? submission_handle->state->executorName
                                    : SmallString{"inline"};
    async_submission.desc.label = submission_handle->state->taskLabel;
    async_submission.desc.domain = async_domain_from_abi_code(desc->domain_code);
    async_submission.desc.queue = async_queue_from_abi_code(desc->queue_code);
    async_submission.desc.priority = static_cast<int>(desc->priority);
    async_submission.endpoint = make_host_endpoint(async_backend_from_abi_code(desc->backend_code),
                                                   async_submission.desc.queue,
                                                   submission_handle->state->taskLabel);
    async_submission.cancellation = submission_handle->state->cancellation.token();

    auto state = submission_handle->state;
    async_submission.step = [state](AsyncExecutionContext &ctx) {
      zpc_runtime_host_task_context_t host_context{};
      host_context.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_task_context_t));
      host_context.submission_id = ctx.submissionId;
      host_context.stop_requested = ctx.cancellation.stop_requested() ? 1u : 0u;
      host_context.interrupt_requested = ctx.cancellation.interrupt_requested() ? 1u : 0u;
      for (auto &slot : host_context.reserved) slot = 0;
      return async_poll_status_from_host_task_result(
          state->task(state->userData, &host_context, &state->desc));
    };

    try {
      submission_handle->handle = engine->runtime->submit(zs::move(async_submission));
    } catch (...) {
      delete submission_handle;
      return ZPC_RUNTIME_ABI_ERROR;
    }

    *submission = submission_handle;
    return ZPC_RUNTIME_ABI_OK;
  }

  inline zpc_runtime_engine_handle_t *make_async_runtime_abi_engine(
      const AsyncRuntimeAbiEngineConfig &config = {}) {
    auto *engine = new zpc_runtime_engine_handle_t{};
    engine->runtime = std::make_unique<AsyncRuntime>(config.workerCount);
    engine->engine_name = config.engineName;
    engine->build_id = config.buildId;
    engine->capability_mask = config.capabilityMask;

    engine->host_submit_extension.header =
        zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_submit_extension_v1_t));
    engine->host_submit_extension.payload_reserved_index = zpc_runtime_host_submit_payload_slot;
    engine->host_submit_extension.reserved0 = 0;
    for (auto &slot : engine->host_submit_extension.reserved) slot = 0;

    engine->table.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_v1_t));
    engine->table.query_engine_desc = &zpc_runtime_async_query_engine_desc;
    engine->table.submit = &zpc_runtime_async_submit;
    engine->table.query_event = &zpc_runtime_async_query_event;
    engine->table.cancel = &zpc_runtime_async_cancel;
    engine->table.release_submission = &zpc_runtime_async_release_submission;
    engine->table.query_extension = &zpc_runtime_async_query_extension;
    for (auto &slot : engine->table.reserved) slot = nullptr;
    return engine;
  }

  inline void destroy_async_runtime_abi_engine(zpc_runtime_engine_handle_t *engine) noexcept {
    delete engine;
  }

  inline const zpc_runtime_engine_v1_t *async_runtime_abi_engine_table(
      const zpc_runtime_engine_handle_t *engine) noexcept {
    return engine ? &engine->table : nullptr;
  }

  static_assert(sizeof(zpc_runtime_abi_header_t) == 16, "ABI header layout must remain fixed");
  static_assert(sizeof(zpc_runtime_string_view_t) == 16, "ABI string view layout must remain fixed");
  static_assert(offsetof(zpc_runtime_engine_v1_t, header) == 0,
                "Engine ABI table header must stay at offset 0");
  static_assert(offsetof(zpc_runtime_engine_v1_t, query_engine_desc) == sizeof(zpc_runtime_abi_header_t),
                "Engine ABI function table order must remain append-only");
  static_assert(sizeof(zpc_runtime_host_task_context_t) == 64,
                "Host task context layout must remain fixed");
  static_assert(offsetof(zpc_runtime_host_submit_payload_t, task)
                    == sizeof(zpc_runtime_abi_header_t),
                "Host submit payload function pointer order must remain append-only");

}  // namespace zs
#endif
