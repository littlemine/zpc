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

namespace zs {

  static_assert(sizeof(zpc_runtime_abi_header_t) == 16, "ABI header layout must remain fixed");
  static_assert(sizeof(zpc_runtime_string_view_t) == 16, "ABI string view layout must remain fixed");
  static_assert(offsetof(zpc_runtime_engine_v1_t, header) == 0,
                "Engine ABI table header must stay at offset 0");
  static_assert(offsetof(zpc_runtime_engine_v1_t, query_engine_desc) == sizeof(zpc_runtime_abi_header_t),
                "Engine ABI function table order must remain append-only");

}  // namespace zs
#endif
