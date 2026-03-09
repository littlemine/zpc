#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "zensim/ZpcAsync.hpp"
#include "zensim/interface/InterfaceServices.hpp"

namespace zs {

  class LocalInterfaceServices final : public InterfaceSessionService,
                                       public InterfaceRuntimeControlService,
                                       public InterfaceValidationService,
                                       public InterfaceResourceService {
  public:
    explicit LocalInterfaceServices(AsyncRuntime &runtime,
                                    AsyncResourceManager *resourceManager = nullptr)
        : _runtime{runtime}, _resourceManager{resourceManager} {
      _knownExecutors.push_back("inline");
      _knownExecutors.push_back("thread_pool");
    }

    void remember_executor(const SmallString &executor) {
      if (executor.size() == 0) return;
      std::lock_guard<std::mutex> lock(_mutex);
      for (const auto &known : _knownExecutors)
        if (known == executor) return;
      _knownExecutors.push_back(executor);
    }

    bool session_exists(InterfaceSessionHandle session) const {
      std::lock_guard<std::mutex> lock(_mutex);
      return find_session_(session) != nullptr;
    }

    bool publish_validation(InterfaceSessionHandle session, ValidationSuiteReport report,
                            const ValidationComparisonReport *comparison = nullptr) {
      std::lock_guard<std::mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      state->latestReport = zs::move(report);
      state->latestReport.refresh_summary();
      state->latestSnapshot.suite = state->latestReport.suite;
      state->latestSnapshot.schemaVersion = state->latestReport.schemaVersion;
      state->latestSnapshot.summary = state->latestReport.summary;
      state->latestSnapshot.hasComparison = comparison != nullptr;
      if (comparison) {
        state->latestComparison = *comparison;
        state->latestSnapshot.comparison = comparison->summary;
      } else {
        state->latestComparison = {};
        state->latestSnapshot.comparison = {};
      }
      return true;
    }

    InterfaceSessionHandle open_session(const InterfaceSessionDescriptor &descriptor) override {
      if (descriptor.label.size() == 0) return {};
      std::lock_guard<std::mutex> lock(_mutex);
      SessionState state{};
      const auto handle = InterfaceSessionHandle{_nextSessionId.fetch_add(1) + 1};
      state.handle = handle;
      state.descriptor = descriptor;
      _sessions.insert_or_assign(handle.id, zs::move(state));
      return handle;
    }

    bool close_session(InterfaceSessionHandle session) override {
      std::lock_guard<std::mutex> lock(_mutex);
      return _sessions.erase(session.id) != 0;
    }

    bool describe_session(InterfaceSessionHandle session,
                          InterfaceSessionDescriptor *descriptor) const override {
      if (descriptor == nullptr) return false;
      std::lock_guard<std::mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      *descriptor = state->descriptor;
      return true;
    }

    bool query_capabilities(InterfaceSessionHandle session,
                            InterfaceCapabilitySnapshot *capabilities) const override {
      if (capabilities == nullptr) return false;
      std::lock_guard<std::mutex> lock(_mutex);
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
      record.cancellation = AsyncStopSource{};
      submission.cancellation = record.cancellation.token();
      remember_executor(submission.executor);
      auto handle = _runtime.submit(zs::move(submission));
      if (!handle.valid()) return {};

      record.handle = handle;
      record.executor = submission_executor_name_(handle, executorName);
      record.label = submissionLabel;
      {
        std::lock_guard<std::mutex> lock(_mutex);
        auto *state = find_session_(session);
        if (!state) return {};
        state->submissions.insert_or_assign(handle.id(), zs::move(record));
      }
      return handle;
    }

    bool query_submission(InterfaceSessionHandle session, u64 submissionId,
                          InterfaceSubmissionSummary *summary) const override {
      if (summary == nullptr) return false;
      std::lock_guard<std::mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      auto it = state->submissions.find(submissionId);
      if (it == state->submissions.end()) return false;
      summary->submissionId = submissionId;
      summary->status = it->second.handle.status();
      summary->backend = AsyncBackend::inline_host;
      summary->queue = AsyncQueueClass::control;
      summary->executor = it->second.executor;
      summary->label = it->second.label;
      return true;
    }

    bool cancel_submission(InterfaceSessionHandle session, u64 submissionId) override {
      std::lock_guard<std::mutex> lock(_mutex);
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
      std::lock_guard<std::mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      *snapshot = state->latestSnapshot;
      return true;
    }

    bool latest_report(InterfaceSessionHandle session,
                       ValidationSuiteReport *report) const override {
      if (report == nullptr) return false;
      std::lock_guard<std::mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state) return false;
      *report = state->latestReport;
      return true;
    }

    bool latest_comparison(InterfaceSessionHandle session,
                           ValidationComparisonReport *report) const override {
      if (report == nullptr) return false;
      std::lock_guard<std::mutex> lock(_mutex);
      auto *state = find_session_(session);
      if (!state || !state->latestSnapshot.hasComparison) return false;
      *report = state->latestComparison;
      return true;
    }

    std::vector<InterfaceResourceInfo> list_resources(InterfaceSessionHandle session) const override {
      if (!_resourceManager || !session.valid()) return {};
      std::lock_guard<std::mutex> lock(_mutex);
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
      std::lock_guard<std::mutex> lock(_mutex);
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
      std::lock_guard<std::mutex> lock(_mutex);
      for (const auto &known : _resourceHandles)
        if (known == resource) return;
      _resourceHandles.push_back(resource);
    }

  private:
    struct SubmissionRecord {
      AsyncSubmissionHandle handle{};
      AsyncStopSource cancellation{};
      SmallString executor{"inline"};
      SmallString label{"submission"};
    };

    struct SessionState {
      InterfaceSessionHandle handle{};
      InterfaceSessionDescriptor descriptor{};
      std::unordered_map<u64, SubmissionRecord> submissions{};
      InterfaceValidationSnapshot latestSnapshot{};
      ValidationSuiteReport latestReport{};
      ValidationComparisonReport latestComparison{};
    };

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
    mutable std::mutex _mutex{};
    Atomic<u64> _nextSessionId{0};
    std::unordered_map<u64, SessionState> _sessions{};
    std::vector<SmallString> _knownExecutors{};
    std::vector<AsyncResourceHandle> _resourceHandles{};
  };

}  // namespace zs