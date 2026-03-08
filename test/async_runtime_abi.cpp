#include <cassert>

#include "zensim/execution/AsyncRuntimeAbi.hpp"

namespace {

  int32_t query_engine_desc(zpc_runtime_engine_handle_t *, zpc_runtime_engine_desc_t *desc) {
    if (!desc) return ZPC_RUNTIME_ABI_ERROR;
    desc->header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_desc_t));
    desc->engine_name = {"zpc-test-engine", 15};
    desc->build_id = {"local", 5};
    desc->capability_mask = ZPC_RUNTIME_ABI_CAP_ASYNC_SUBMIT | ZPC_RUNTIME_ABI_CAP_HOT_UPGRADE;
    return ZPC_RUNTIME_ABI_OK;
  }

  int32_t submit(zpc_runtime_engine_handle_t *, const zpc_runtime_submission_desc_t *,
                 zpc_runtime_submission_handle_t **) {
    return ZPC_RUNTIME_ABI_OK;
  }

  int32_t query_event(zpc_runtime_submission_handle_t *, zpc_runtime_host_event_t *eventDesc) {
    if (!eventDesc) return ZPC_RUNTIME_ABI_ERROR;
    eventDesc->header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_host_event_t));
    eventDesc->status_code = 3;
    eventDesc->native_signal_token = 7;
    return ZPC_RUNTIME_ABI_OK;
  }

  int32_t cancel(zpc_runtime_submission_handle_t *) { return ZPC_RUNTIME_ABI_OK; }
  int32_t release_submission(zpc_runtime_submission_handle_t *) { return ZPC_RUNTIME_ABI_OK; }
  int32_t query_extension(zpc_runtime_engine_handle_t *, zpc_runtime_string_view_t,
                          zpc_runtime_extension_desc_t *extensionDesc) {
    if (!extensionDesc) return ZPC_RUNTIME_ABI_ERROR;
    extensionDesc->header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_extension_desc_t));
    extensionDesc->extension_name = {"ext.validation", 14};
    extensionDesc->extension_version_major = 1;
    extensionDesc->extension_version_minor = 0;
    extensionDesc->function_table = nullptr;
    return ZPC_RUNTIME_ABI_OK;
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

  zpc_runtime_engine_v1_t engineTable{};
  engineTable.header = zpc_runtime_make_header((uint32_t)sizeof(zpc_runtime_engine_v1_t));
  engineTable.query_engine_desc = &query_engine_desc;
  engineTable.submit = &submit;
  engineTable.query_event = &query_event;
  engineTable.cancel = &cancel;
  engineTable.release_submission = &release_submission;
  engineTable.query_extension = &query_extension;
  assert(zpc_runtime_check_engine_table(&engineTable) == ZPC_RUNTIME_ABI_OK);

  zpc_runtime_engine_desc_t engineDesc{};
  assert(engineTable.query_engine_desc(nullptr, &engineDesc) == ZPC_RUNTIME_ABI_OK);
  assert(engineDesc.engine_name.size == 15);
  assert(engineDesc.capability_mask & ZPC_RUNTIME_ABI_CAP_HOT_UPGRADE);

  zpc_runtime_host_event_t eventDesc{};
  assert(engineTable.query_event(nullptr, &eventDesc) == ZPC_RUNTIME_ABI_OK);
  assert(eventDesc.status_code == 3);
  assert(eventDesc.native_signal_token == 7);

  zpc_runtime_extension_desc_t extensionDesc{};
  assert(engineTable.query_extension(nullptr, {"ext.validation", 14}, &extensionDesc)
         == ZPC_RUNTIME_ABI_OK);
  assert(extensionDesc.extension_version_major == 1);
  assert(extensionDesc.extension_version_minor == 0);

  return 0;
}