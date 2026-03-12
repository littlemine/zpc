#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "zensim/ZpcAsync.hpp"
#include "zensim/execution/ValidationFormat.hpp"
#include "zensim/interface/InterfaceServices.hpp"

namespace zs {

  class LocalInterfaceServices final : public InterfaceSessionService,
                                       public InterfaceRuntimeControlService,
                                       public InterfaceValidationService,
                                       public InterfaceScenarioService,
                                       public InterfaceResourceService {
  public:
    explicit LocalInterfaceServices(AsyncRuntime &runtime,
                                    AsyncResourceManager *resourceManager = nullptr)
        : _runtime{runtime}, _resourceManager{resourceManager} {
      _knownExecutors.push_back("inline");
      _knownExecutors.push_back("thread_pool");
    }

    void attach_scenario_service(const InterfaceScenarioService *scenarioService) {
      std::lock_guard<Mutex> lock(_mutex);
      _scenarioService = scenarioService;
    }

    void remember_executor(const SmallString &executor) {
      if (executor.size() == 0) return;
      std::lock_guard<Mutex> lock(_mutex);
      for (const auto &known : _knownExecutors)
        if (known == executor) return;
      _knownExecutors.push_back(executor);
    }

    bool session_exists(InterfaceSessionHandle session) const {
      std::lock_guard<Mutex> lock(_mutex);
      return find_session_(session) != nullptr;
    }

    bool publish_validation(InterfaceSessionHandle session, ValidationSuiteReport report,
                            const ValidationComparisonReport *comparison = nullptr) {
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      report.refresh_summary();
      const auto reportId = _nextReportId.fetch_add(1) + 1;
      ValidationHistoryEntry entry{};
      entry.snapshot.reportId = reportId;
      entry.snapshot.suite = report.suite;
      entry.snapshot.schemaVersion = report.schemaVersion;
      entry.snapshot.summary = report.summary;
      entry.snapshot.hasComparison = comparison != nullptr;
      entry.artifacts = make_validation_artifacts_(entry.snapshot, report.suite,
                                                   comparison != nullptr);
      entry.report = zs::move(report);
      if (comparison) {
        entry.comparison = *comparison;
        entry.snapshot.comparison = comparison->summary;
      } else {
        entry.comparison = {};
        entry.snapshot.comparison = {};
      }
      state->latestReport = entry.report;
      state->latestSnapshot = entry.snapshot;
      state->latestComparison = entry.comparison;
      state->validationHistory.push_back(zs::move(entry));
      if (state->validationHistory.size() > validation_history_limit)
        state->validationHistory.erase(state->validationHistory.begin());
      return true;
    }

    InterfaceSessionHandle open_session(const InterfaceSessionDescriptor &descriptor) override {
      if (descriptor.label.size() == 0) return {};
      std::lock_guard<Mutex> lock(_mutex);
      SessionState state{};
      const auto handle = InterfaceSessionHandle{_nextSessionId.fetch_add(1) + 1};
      state.handle = handle;
      state.descriptor = descriptor;
      _sessions.insert_or_assign(handle.id, zs::move(state));
      return handle;
    }

    bool close_session(InterfaceSessionHandle session) override {
      std::lock_guard<Mutex> lock(_mutex);
      return _sessions.erase(session.id) != 0;
    }

    bool describe_session(InterfaceSessionHandle session,
                          InterfaceSessionDescriptor *descriptor) const override {
      if (descriptor == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      *descriptor = state->descriptor;
      return true;
    }

    bool query_capabilities(InterfaceSessionHandle session,
                            InterfaceCapabilitySnapshot *capabilities) const override {
      if (capabilities == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      capabilities->profile = state->descriptor.profile.size() ? state->descriptor.profile : "runtime";
      capabilities->backends.clear();
      capabilities->queues.clear();
      capabilities->executors.clear();
      for (const auto &executor : _knownExecutors) {
        if (_runtime.contains_executor(executor.asChars())) {
          capabilities->executors.push_back(executor);
        }
      }
      if (!capabilities->executors.empty()) {
        capabilities->backends.push_back(AsyncBackend::inline_host);
        capabilities->queues.push_back(AsyncQueueClass::control);
      }
      capabilities->supportsValidationReports = true;
      capabilities->supportsBenchmarkReports = true;
      capabilities->supportsResourceInspection = _resourceManager != nullptr;
      capabilities->supportsScenarioAccess = true;
      return true;
    }

    AsyncSubmissionHandle submit(InterfaceSessionHandle session, AsyncSubmission submission) override {
      SubmissionRecord record{};
      if (!session.valid()) return {};
      if (submission.executor.size() == 0) submission.executor = "inline";
      if (submission.desc.label.size() == 0) submission.desc.label = "submission";
      const auto executorName = submission.executor;
      const auto submissionLabel = submission.desc.label;
      const auto submissionQueue = submission.desc.queue;
      const auto submissionBackend = infer_backend_(executorName, submission.endpoint);
      record.cancellation = AsyncStopSource{};
      submission.cancellation = record.cancellation.token();
      remember_executor(submission.executor);
      auto handle = _runtime.submit(zs::move(submission));
      if (!handle.valid()) return {};

      record.handle = handle;
      record.executor = submission_executor_name_(handle, executorName);
      record.label = submissionLabel;
      record.backend = submissionBackend;
      record.queue = submissionQueue;
      {
        std::lock_guard<Mutex> lock(_mutex);
        auto *state = find_session_(session);
        if (!state) return {};
        state->submissions.insert_or_assign(handle.id(), zs::move(record));
      }
      return handle;
    }

    bool query_submission(InterfaceSessionHandle session, u64 submissionId,
                          InterfaceSubmissionSummary *summary) const override {
      if (summary == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      auto it = state->submissions.find(submissionId);
      if (it == state->submissions.end()) return false;
      summary->submissionId = submissionId;
      summary->status = it->second.handle.status();
      summary->backend = it->second.backend;
      summary->queue = it->second.queue;
      summary->executor = it->second.executor;
      summary->label = it->second.label;
      return true;
    }

    bool cancel_submission(InterfaceSessionHandle session, u64 submissionId) override {
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      auto it = state->submissions.find(submissionId);
      if (it == state->submissions.end()) return false;
      it->second.cancellation.request_stop();
      return true;
    }

    bool latest_snapshot(InterfaceSessionHandle session,
                         InterfaceValidationSnapshot *snapshot) const override {
      if (snapshot == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      *snapshot = state->latestSnapshot;
      return true;
    }

    bool latest_report(InterfaceSessionHandle session,
                       ValidationSuiteReport *report) const override {
      if (report == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      *report = state->latestReport;
      return true;
    }

    bool latest_comparison(InterfaceSessionHandle session,
                           ValidationComparisonReport *report) const override {
      if (report == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state || !state->latestSnapshot.hasComparison) return false;
      *report = state->latestComparison;
      return true;
    }

    std::vector<InterfaceValidationSnapshot> list_snapshots(
        InterfaceSessionHandle session) const override {
      if (!session.valid()) return {};
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return {};
      std::vector<InterfaceValidationSnapshot> snapshots;
      snapshots.reserve(state->validationHistory.size());
      for (const auto &entry : state->validationHistory) snapshots.push_back(entry.snapshot);
      return snapshots;
    }

    bool snapshot(InterfaceSessionHandle session, u64 reportId,
                  InterfaceValidationSnapshot *snapshot) const override {
      if (snapshot == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      const auto *entry = find_validation_history_(state->validationHistory, reportId);
      if (!entry) return false;
      *snapshot = entry->snapshot;
      return true;
    }

    bool report(InterfaceSessionHandle session, u64 reportId,
                ValidationSuiteReport *report) const override {
      if (report == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      const auto *entry = find_validation_history_(state->validationHistory, reportId);
      if (!entry) return false;
      *report = entry->report;
      return true;
    }

    bool comparison(InterfaceSessionHandle session, u64 reportId,
                    ValidationComparisonReport *report) const override {
      if (report == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      const auto *entry = find_validation_history_(state->validationHistory, reportId);
      if (!entry || !entry->snapshot.hasComparison) return false;
      *report = entry->comparison;
      return true;
    }

    bool format_latest_report(InterfaceSessionHandle session, InterfaceReportFormat format,
                              std::string *output) const override {
      if (output == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state || state->validationHistory.empty()) return false;
      return format_validation_report_(state->validationHistory.back().report, format, output);
    }

    bool format_latest_comparison(InterfaceSessionHandle session, InterfaceReportFormat format,
                                  std::string *output) const override {
      if (output == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state || state->validationHistory.empty()) return false;
      const auto &entry = state->validationHistory.back();
      if (!entry.snapshot.hasComparison) return false;
      return format_validation_comparison_(entry.comparison, format, output);
    }

    bool format_report(InterfaceSessionHandle session, u64 reportId, InterfaceReportFormat format,
                       std::string *output) const override {
      if (output == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      const auto *entry = find_validation_history_(state->validationHistory, reportId);
      if (!entry) return false;
      return format_validation_report_(entry->report, format, output);
    }

    bool format_comparison(InterfaceSessionHandle session, u64 reportId,
                           InterfaceReportFormat format, std::string *output) const override {
      if (output == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      const auto *entry = find_validation_history_(state->validationHistory, reportId);
      if (!entry || !entry->snapshot.hasComparison) return false;
      return format_validation_comparison_(entry->comparison, format, output);
    }

    std::vector<InterfaceArtifactInfo> list_artifacts(InterfaceSessionHandle session) const override {
      if (!session.valid()) return {};
      std::lock_guard<Mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return {};
      std::vector<InterfaceArtifactInfo> artifacts;
      for (const auto &entry : state->validationHistory) {
        artifacts.insert(artifacts.end(), entry.artifacts.begin(), entry.artifacts.end());
      }
      return artifacts;
    }

    std::vector<InterfaceScenarioDescriptor> list_interface_scenarios(
        InterfaceSessionHandle session) const override;
    bool describe_interface_scenario(InterfaceSessionHandle session, const SmallString &scenarioId,
                                     InterfaceScenarioDescriptor *descriptor) const override;

    std::vector<InterfaceResourceInfo> list_resources(InterfaceSessionHandle session) const override {
      if (!_resourceManager || !session.valid()) return {};
      std::lock_guard<Mutex> lock(_mutex);
      if (!find_session_(session)) return {};
      std::vector<InterfaceResourceInfo> resources;
      for (const auto &entry : _resourceHandles) {
        InterfaceResourceInfo info{};
        if (query_resource_locked_(entry, &info)) resources.push_back(zs::move(info));
      }
      return resources;
    }

    bool query_resource(InterfaceSessionHandle session, AsyncResourceHandle resource,
                        InterfaceResourceInfo *info) const override {
      if (!_resourceManager || !session.valid() || info == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      if (!find_session_(session)) return false;
      return query_resource_locked_(resource, info);
    }

    bool touch_resource(InterfaceSessionHandle session, AsyncResourceHandle resource) override {
      return _resourceManager && session_exists(session) && _resourceManager->touch(resource);
    }

    bool mark_resource_dirty(InterfaceSessionHandle session, AsyncResourceHandle resource,
                             bool dirty = true) override {
      return _resourceManager && session_exists(session) && _resourceManager->mark_dirty(resource, dirty);
    }

    void remember_resource(AsyncResourceHandle resource) {
      if (!resource.valid()) return;
      std::lock_guard<Mutex> lock(_mutex);
      for (const auto &known : _resourceHandles)
        if (known == resource) return;
      _resourceHandles.push_back(resource);
    }

  private:
    static constexpr size_t validation_history_limit = 16;

    struct SubmissionRecord {
      AsyncSubmissionHandle handle{};
      AsyncStopSource cancellation{};
      SmallString executor{"inline"};
      SmallString label{"submission"};
      AsyncBackend backend{AsyncBackend::inline_host};
      AsyncQueueClass queue{AsyncQueueClass::control};
    };

    struct ValidationHistoryEntry {
      InterfaceValidationSnapshot snapshot{};
      ValidationSuiteReport report{};
      ValidationComparisonReport comparison{};
      std::vector<InterfaceArtifactInfo> artifacts{};
    };

    struct SessionState {
      InterfaceSessionHandle handle{};
      InterfaceSessionDescriptor descriptor{};
      std::unordered_map<u64, SubmissionRecord> submissions{};
      InterfaceValidationSnapshot latestSnapshot{};
      ValidationSuiteReport latestReport{};
      ValidationComparisonReport latestComparison{};
      std::vector<ValidationHistoryEntry> validationHistory{};
    };

    static const ValidationHistoryEntry *find_validation_history_(
        const std::vector<ValidationHistoryEntry> &history, u64 reportId) {
      for (const auto &entry : history)
        if (entry.snapshot.reportId == reportId) return &entry;
      return nullptr;
    }

    SessionState *find_session_(InterfaceSessionHandle session) {
      auto it = _sessions.find(session.id);
      return it == _sessions.end() ? nullptr : &it->second;
    }

    const SessionState *find_session_(InterfaceSessionHandle session) const {
      auto it = _sessions.find(session.id);
      return it == _sessions.end() ? nullptr : &it->second;
    }

    static SmallString submission_executor_name_(const AsyncSubmissionHandle &, const SmallString &fallback) {
      return fallback.size() ? fallback : SmallString{"inline"};
    }

    static SmallString make_artifact_id_(const char *category, u64 reportId) {
      return SmallString{std::string{category} + "-" + std::to_string(reportId)};
    }

    static SmallString make_artifact_path_(const SmallString &suite, const char *category,
                                           u64 reportId, const char *extension) {
      std::string path{"artifacts/validation/"};
      path += suite.size() ? suite.asChars() : "suite";
      path += '/';
      path += category;
      path += '-';
      path += std::to_string(reportId);
      path += '.';
      path += extension;
      return SmallString{path};
    }

    static std::vector<InterfaceArtifactInfo> make_validation_artifacts_(
        const InterfaceValidationSnapshot &snapshot, const SmallString &suite, bool hasComparison) {
      std::vector<InterfaceArtifactInfo> artifacts;
      artifacts.reserve(hasComparison ? 4 : 2);

      InterfaceArtifactInfo summary{};
      summary.artifactId = make_artifact_id_("summary", snapshot.reportId);
      summary.category = "validation-summary";
      summary.format = "text";
      summary.path = make_artifact_path_(suite, "summary", snapshot.reportId, "txt");
      summary.suite = suite;
      summary.reportId = snapshot.reportId;
      summary.available = true;
      artifacts.push_back(zs::move(summary));

      InterfaceArtifactInfo report{};
      report.artifactId = make_artifact_id_("report", snapshot.reportId);
      report.category = "validation-report";
      report.format = "json";
      report.path = make_artifact_path_(suite, "report", snapshot.reportId, "json");
      report.suite = suite;
      report.reportId = snapshot.reportId;
      report.available = true;
      artifacts.push_back(zs::move(report));

      if (hasComparison) {
        InterfaceArtifactInfo comparisonSummary{};
        comparisonSummary.artifactId = make_artifact_id_("comparison-summary", snapshot.reportId);
        comparisonSummary.category = "validation-comparison-summary";
        comparisonSummary.format = "text";
        comparisonSummary.path = make_artifact_path_(suite, "comparison-summary", snapshot.reportId,
                                                     "txt");
        comparisonSummary.suite = suite;
        comparisonSummary.reportId = snapshot.reportId;
        comparisonSummary.available = true;
        artifacts.push_back(zs::move(comparisonSummary));

        InterfaceArtifactInfo comparison{};
        comparison.artifactId = make_artifact_id_("comparison", snapshot.reportId);
        comparison.category = "validation-comparison";
        comparison.format = "json";
        comparison.path = make_artifact_path_(suite, "comparison", snapshot.reportId, "json");
        comparison.suite = suite;
        comparison.reportId = snapshot.reportId;
        comparison.available = true;
        artifacts.push_back(zs::move(comparison));
      }

      return artifacts;
    }

    static AsyncBackend infer_backend_(const SmallString &executor, const AsyncEndpoint &endpoint) {
      if (endpoint.backend != AsyncBackend::inline_host || endpoint.nativeHandle != nullptr
          || endpoint.device >= 0 || endpoint.stream >= 0)
        return endpoint.backend;
      if (executor == "thread_pool") return AsyncBackend::thread_pool;
      if (executor == "inline") return AsyncBackend::inline_host;
      return endpoint.backend;
    }

    static bool format_validation_report_(const ValidationSuiteReport &report,
                                          InterfaceReportFormat format, std::string *output) {
      switch (format) {
        case InterfaceReportFormat::summary:
          *output = format_validation_summary_text(report);
          return true;
        case InterfaceReportFormat::text:
          *output = format_validation_report_text(report);
          return true;
        case InterfaceReportFormat::json:
          *output = format_validation_report_json(report);
          return true;
        default:
          return false;
      }
    }

    static bool format_validation_comparison_(const ValidationComparisonReport &comparison,
                                              InterfaceReportFormat format,
                                              std::string *output) {
      switch (format) {
        case InterfaceReportFormat::summary:
          *output = format_validation_comparison_summary_text(comparison);
          return true;
        case InterfaceReportFormat::text:
          *output = format_validation_comparison_report_text(comparison);
          return true;
        case InterfaceReportFormat::json:
          *output = format_validation_comparison_report_json(comparison);
          return true;
        default:
          return false;
      }
    }

    bool query_resource_locked_(AsyncResourceHandle resource, InterfaceResourceInfo *info) const {
      const AsyncResourceDescriptor *descriptor = nullptr;
      AsyncResourceStateSnapshot state{};
      if (!_resourceManager->describe(resource, &descriptor, &state) || descriptor == nullptr) return false;
      info->handle = resource;
      info->label = descriptor->label;
      info->executor = descriptor->executor;
      info->domain = descriptor->domain;
      info->queue = descriptor->queue;
      info->bytes = descriptor->bytes;
      info->leaseCount = state.leaseCount;
      info->lastAccessEpoch = state.lastAccessEpoch;
      info->dirty = state.dirty;
      info->busy = state.busy;
      info->retired = state.retired;
      info->stale = state.stale;
      return true;
    }

    AsyncRuntime &_runtime;
    AsyncResourceManager *_resourceManager{nullptr};
    const InterfaceScenarioService *_scenarioService{nullptr};
    mutable Mutex _mutex{};
    Atomic<u64> _nextSessionId{0};
    Atomic<u64> _nextReportId{0};
    std::unordered_map<u64, SessionState> _sessions{};
    std::vector<SmallString> _knownExecutors{};
    std::vector<AsyncResourceHandle> _resourceHandles{};
  };

}  // namespace zs

namespace zs {

  inline std::vector<InterfaceScenarioDescriptor> LocalInterfaceServices::list_interface_scenarios(
      InterfaceSessionHandle session) const {
    const InterfaceScenarioService *scenarioService = nullptr;
    {
      std::lock_guard<Mutex> lock(_mutex);
      if (!find_session_(session)) return {};
      scenarioService = _scenarioService;
    }
    if (!scenarioService) return {};
    return scenarioService->list_interface_scenarios(session);
  }

  inline bool LocalInterfaceServices::describe_interface_scenario(
      InterfaceSessionHandle session, const SmallString &scenarioId,
      InterfaceScenarioDescriptor *descriptor) const {
    const InterfaceScenarioService *scenarioService = nullptr;
    {
      std::lock_guard<Mutex> lock(_mutex);
      if (!find_session_(session)) return false;
      scenarioService = _scenarioService;
    }
    return scenarioService
        && scenarioService->describe_interface_scenario(session, scenarioId, descriptor);
  }

}  // namespace zs
