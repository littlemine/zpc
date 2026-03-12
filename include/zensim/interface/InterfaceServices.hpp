#pragma once

#include <string>
#include <vector>

#include "zensim/execution/AsyncResourceManager.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/ValidationCompare.hpp"
#include "zensim/execution/ValidationSchema.hpp"

namespace zs {

  class CanaryScenarioService;

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
  enum class InterfaceScenarioKind : u8 { generic, canary, audio, custom };

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
    u64 reportId{0};
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

  struct InterfaceScenarioDescriptor {
    SmallString scenarioId{};
    SmallString label{};
    SmallString description{};
    SmallString version{};
    InterfaceScenarioKind kind{InterfaceScenarioKind::generic};
    std::vector<SmallString> systems{};
    std::vector<SmallString> metrics{};
    std::vector<ValidationMetadataEntry> metadata{};
  };

  struct InterfaceArtifactInfo {
    SmallString artifactId{};
    SmallString category{};
    SmallString format{};
    SmallString path{};
    SmallString suite{};
    u64 reportId{0};
    bool available{false};
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
    virtual std::vector<InterfaceValidationSnapshot> list_snapshots(
        InterfaceSessionHandle session) const = 0;
    virtual bool snapshot(InterfaceSessionHandle session, u64 reportId,
                          InterfaceValidationSnapshot *snapshot) const = 0;
    virtual bool report(InterfaceSessionHandle session, u64 reportId,
                        ValidationSuiteReport *report) const = 0;
    virtual bool comparison(InterfaceSessionHandle session, u64 reportId,
                            ValidationComparisonReport *report) const = 0;
    virtual bool format_latest_report(InterfaceSessionHandle session, InterfaceReportFormat format,
                                      std::string *output) const = 0;
    virtual bool format_latest_comparison(InterfaceSessionHandle session,
                                          InterfaceReportFormat format,
                                          std::string *output) const = 0;
    virtual bool format_report(InterfaceSessionHandle session, u64 reportId,
                               InterfaceReportFormat format,
                               std::string *output) const = 0;
    virtual bool format_comparison(InterfaceSessionHandle session, u64 reportId,
                                   InterfaceReportFormat format,
                                   std::string *output) const = 0;
    virtual std::vector<InterfaceArtifactInfo> list_artifacts(
        InterfaceSessionHandle session) const = 0;
  };

  class InterfaceScenarioService {
  public:
    virtual ~InterfaceScenarioService() = default;

    virtual std::vector<InterfaceScenarioDescriptor> list_interface_scenarios(
        InterfaceSessionHandle session) const = 0;
    virtual bool describe_interface_scenario(InterfaceSessionHandle session,
                                             const SmallString &scenarioId,
                                             InterfaceScenarioDescriptor *descriptor) const = 0;
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
    InterfaceScenarioService *scenarios{nullptr};
    InterfaceResourceService *resources{nullptr};
    CanaryScenarioService *canary{nullptr};

    bool complete() const noexcept {
      return sessions && runtime && validation && scenarios && resources && canary;
    }
  };

}  // namespace zs
