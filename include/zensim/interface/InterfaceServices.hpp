#pragma once

#include <vector>

#include "zensim/execution/AsyncResourceManager.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/ValidationCompare.hpp"
#include "zensim/execution/ValidationSchema.hpp"

namespace zs {

  struct InterfaceSessionHandle {
    u64 id{0};

    constexpr explicit operator bool() const noexcept { return id != 0; }
    constexpr bool valid() const noexcept { return id != 0; }
    friend constexpr bool operator==(InterfaceSessionHandle lhs,
                                     InterfaceSessionHandle rhs) noexcept {
      return lhs.id == rhs.id;
    }
    friend constexpr bool operator!=(InterfaceSessionHandle lhs,
                                     InterfaceSessionHandle rhs) noexcept {
      return !(lhs == rhs);
    }
  };

  enum class InterfaceTransportKind : u8 { in_process, local_ipc, http, websocket, custom };
  enum class InterfaceConsumerKind : u8 { cli, gui, web, mcp, custom };
  enum class InterfaceReportFormat : u8 { summary, text, json };

  struct InterfaceSessionDescriptor {
    SmallString label{};
    SmallString user{};
    SmallString profile{};
    InterfaceTransportKind transport{InterfaceTransportKind::in_process};
    InterfaceConsumerKind consumer{InterfaceConsumerKind::custom};
    bool remote{false};
  };

  struct InterfaceCapabilitySnapshot {
    SmallString profile{};
    std::vector<AsyncBackend> backends{};
    std::vector<AsyncQueueClass> queues{};
    std::vector<SmallString> executors{};
    bool supportsValidationReports{false};
    bool supportsBenchmarkReports{false};
    bool supportsResourceInspection{false};
    bool supportsScenarioAccess{false};
  };

  struct InterfaceSubmissionSummary {
    u64 submissionId{0};
    AsyncTaskStatus status{AsyncTaskStatus::pending};
    AsyncBackend backend{AsyncBackend::inline_host};
    AsyncQueueClass queue{AsyncQueueClass::control};
    SmallString executor{};
    SmallString label{};
  };

  struct InterfaceValidationSnapshot {
    SmallString suite{};
    SmallString schemaVersion{"zpc.validation.v1"};
    ValidationSummary summary{};
    ValidationComparisonSummary comparison{};
    bool hasComparison{false};
  };

  struct InterfaceResourceInfo {
    AsyncResourceHandle handle{};
    SmallString label{};
    SmallString executor{};
    AsyncDomain domain{AsyncDomain::control};
    AsyncQueueClass queue{AsyncQueueClass::control};
    size_t bytes{0};
    size_t leaseCount{0};
    u64 lastAccessEpoch{0};
    bool dirty{false};
    bool busy{false};
    bool retired{false};
    bool stale{false};
  };

  class InterfaceSessionService {
  public:
    virtual ~InterfaceSessionService() = default;

    virtual InterfaceSessionHandle open_session(const InterfaceSessionDescriptor &descriptor) = 0;
    virtual bool close_session(InterfaceSessionHandle session) = 0;
    virtual bool describe_session(InterfaceSessionHandle session,
                                  InterfaceSessionDescriptor *descriptor) const = 0;
  };

  class InterfaceRuntimeControlService {
  public:
    virtual ~InterfaceRuntimeControlService() = default;

    virtual bool query_capabilities(InterfaceSessionHandle session,
                                    InterfaceCapabilitySnapshot *capabilities) const = 0;
    virtual AsyncSubmissionHandle submit(InterfaceSessionHandle session,
                                         AsyncSubmission submission) = 0;
    virtual bool query_submission(InterfaceSessionHandle session, u64 submissionId,
                                  InterfaceSubmissionSummary *summary) const = 0;
    virtual bool cancel_submission(InterfaceSessionHandle session, u64 submissionId) = 0;
  };

  class InterfaceValidationService {
  public:
    virtual ~InterfaceValidationService() = default;

    virtual bool latest_snapshot(InterfaceSessionHandle session,
                                 InterfaceValidationSnapshot *snapshot) const = 0;
    virtual bool latest_report(InterfaceSessionHandle session,
                               ValidationSuiteReport *report) const = 0;
    virtual bool latest_comparison(InterfaceSessionHandle session,
                                   ValidationComparisonReport *report) const = 0;
  };

  class InterfaceResourceService {
  public:
    virtual ~InterfaceResourceService() = default;

    virtual std::vector<InterfaceResourceInfo> list_resources(
        InterfaceSessionHandle session) const = 0;
    virtual bool query_resource(InterfaceSessionHandle session, AsyncResourceHandle resource,
                                InterfaceResourceInfo *info) const = 0;
    virtual bool touch_resource(InterfaceSessionHandle session, AsyncResourceHandle resource) = 0;
    virtual bool mark_resource_dirty(InterfaceSessionHandle session, AsyncResourceHandle resource,
                                     bool dirty = true) = 0;
  };

  struct InterfaceServiceBundle {
    InterfaceSessionService *sessions{nullptr};
    InterfaceRuntimeControlService *runtime{nullptr};
    InterfaceValidationService *validation{nullptr};
    InterfaceResourceService *resources{nullptr};

    bool complete() const noexcept {
      return sessions && runtime && validation && resources;
    }
  };

}  // namespace zs