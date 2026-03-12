#include <cstdio>
#include <string>

#include "zensim/interface/LocalCanaryService.hpp"

int main() {
  zs::AsyncRuntime runtime{1};
  zs::AsyncResourceManager resourceManager{runtime};
  zs::LocalInterfaceServices services{runtime, &resourceManager};
  zs::LocalCanaryScenarioService canaryService{services};

  zs::InterfaceServiceBundle bundle{&services, &services, &services, &services, &canaryService};
  if (!bundle.complete()) {
    std::fprintf(stderr, "service bundle incomplete\n");
    return 1;
  }

  zs::InterfaceSessionDescriptor descriptor{};
  descriptor.label = "smoke";
  descriptor.profile = "runtime";
  descriptor.consumer = zs::InterfaceConsumerKind::cli;

  const auto session = services.open_session(descriptor);
  if (!session.valid()) {
    std::fprintf(stderr, "failed to open session\n");
    return 1;
  }

  zs::InterfaceSessionDescriptor describedSession{};
  if (!services.describe_session(session, &describedSession) || describedSession.label != "smoke") {
    std::fprintf(stderr, "failed to describe session\n");
    return 1;
  }

  zs::InterfaceCapabilitySnapshot capabilities{};
  if (!services.query_capabilities(session, &capabilities)
      || capabilities.executors.empty()) {
    std::fprintf(stderr, "missing capabilities\n");
    return 1;
  }

  zs::AsyncResourceDescriptor resourceDescriptor{};
  resourceDescriptor.label = "resource";
  auto resource = resourceManager.register_resource(resourceDescriptor);
  services.remember_resource(resource);

  zs::InterfaceResourceInfo resourceInfo{};
  if (!services.query_resource(session, resource, &resourceInfo) || resourceInfo.label != "resource") {
    std::fprintf(stderr, "missing resource info\n");
    return 1;
  }

  const auto resources = services.list_resources(session);
  if (resources.size() != 1 || resources.front().handle != resource) {
    std::fprintf(stderr, "missing resource listing\n");
    return 1;
  }

  zs::AsyncSubmission submission{};
  submission.executor = "inline";
  submission.desc.label = "noop";
  submission.step = [](zs::AsyncExecutionContext &) { return zs::AsyncPollStatus::completed; };
  const auto handle = services.submit(session, zs::move(submission));
  zs::InterfaceSubmissionSummary submissionSummary{};
  if (!handle.valid() || !services.query_submission(session, handle.id(), &submissionSummary)
      || submissionSummary.status != zs::AsyncTaskStatus::completed
      || submissionSummary.backend != zs::AsyncBackend::inline_host) {
    std::fprintf(stderr, "submission flow failed\n");
    return 1;
  }

  zs::AsyncSubmission cancelSubmission{};
  cancelSubmission.executor = "thread_pool";
  cancelSubmission.desc.label = "cancel_me";
  cancelSubmission.desc.queue = zs::AsyncQueueClass::compute;
  cancelSubmission.step = [](zs::AsyncExecutionContext &ctx) {
    while (!ctx.cancellation.stop_requested() && !ctx.cancellation.interrupt_requested()) {
      zs::ManagedThread::yield_current();
    }
    return zs::AsyncPollStatus::cancelled;
  };
  const auto cancelHandle = services.submit(session, zs::move(cancelSubmission));
  if (!cancelHandle.valid() || !services.cancel_submission(session, cancelHandle.id())) {
    std::fprintf(stderr, "failed to cancel submission\n");
    return 1;
  }
  if (!cancelHandle.event().wait_for(1000)) {
    std::fprintf(stderr, "cancelled submission did not finish\n");
    return 1;
  }
  zs::InterfaceSubmissionSummary cancelledSummary{};
  if (!services.query_submission(session, cancelHandle.id(), &cancelledSummary)
      || cancelledSummary.status != zs::AsyncTaskStatus::cancelled
      || cancelledSummary.backend != zs::AsyncBackend::thread_pool
      || cancelledSummary.queue != zs::AsyncQueueClass::compute) {
    std::fprintf(stderr, "cancelled submission summary mismatch\n");
    return 1;
  }

  zs::CanaryScenarioDescriptor scenario{};
  scenario.scenarioId = "demo";
  scenario.label = "Demo";
  zs::CanaryParameterDescriptor speed{};
  speed.name = "speed";
  speed.label = "Speed";
  speed.kind = zs::CanaryParameterKind::floating_point;
  speed.defaultValue = "1.0";
  speed.minValue = "0.0";
  speed.maxValue = "10.0";
  speed.required = true;
  scenario.parameters.push_back(speed);
  canaryService.register_scenario(zs::move(scenario));

  zs::ValidationSuiteReport baseline{};
  baseline.suite = "demo";
  zs::ValidationRecord baselineRecord{};
  baselineRecord.recordId = "demo";
  baselineRecord.suite = "demo";
  baselineRecord.name = "Demo";
  baselineRecord.kind = zs::ValidationRecordKind::benchmark;
  baselineRecord.outcome = zs::ValidationOutcome::pass;
  baseline.records.push_back(zs::move(baselineRecord));
  baseline.refresh_summary();
  canaryService.register_baseline("demo", zs::move(baseline));

  zs::InterfaceValidationSnapshot validation{};
  const auto scenarios = canaryService.list_scenarios(session);
  if (scenarios.empty() || scenarios.front().scenarioId != "demo") {
    std::fprintf(stderr, "missing canary scenario\n");
    return 1;
  }

  zs::CanaryScenarioRunRequest request{};
  request.session = session;
  request.scenarioId = scenarios.front().scenarioId;
  request.baselineId = "demo";
  request.overrides.push_back(zs::CanaryParameterOverride{"speed", "2.5"});
  const auto result = canaryService.run_scenario(request);
  if (!result.accepted || !result.hasComparison) {
    std::fprintf(stderr, "canary scenario rejected\n");
    return 1;
  }

  std::vector<zs::InterfaceValidationSnapshot> snapshots = services.list_snapshots(session);
  if (snapshots.size() != 1 || snapshots.front().reportId == 0 || !snapshots.front().hasComparison) {
    std::fprintf(stderr, "missing validation history\n");
    return 1;
  }

  zs::InterfaceValidationSnapshot firstSnapshot{};
  if (!services.snapshot(session, snapshots.front().reportId, &firstSnapshot)
      || firstSnapshot.summary.total != 2) {
    std::fprintf(stderr, "missing validation snapshot by id\n");
    return 1;
  }

  std::string latestSummary;
  if (!services.format_latest_report(session, zs::InterfaceReportFormat::summary, &latestSummary)
      || latestSummary.find("suite=demo") == std::string::npos) {
    std::fprintf(stderr, "missing formatted latest validation summary\n");
    return 1;
  }

  std::string latestReportJson;
  if (!services.format_report(session, snapshots.front().reportId, zs::InterfaceReportFormat::json,
                              &latestReportJson)
      || latestReportJson.find("\"suite\":\"demo\"") == std::string::npos) {
    std::fprintf(stderr, "missing formatted validation report\n");
    return 1;
  }

  std::string latestComparisonText;
  if (!services.format_latest_comparison(session, zs::InterfaceReportFormat::text,
                                         &latestComparisonText)
      || latestComparisonText.find("accepted=true") == std::string::npos) {
    std::fprintf(stderr, "missing formatted validation comparison\n");
    return 1;
  }

  zs::ValidationComparisonReport firstComparison{};
  if (!services.comparison(session, snapshots.front().reportId, &firstComparison)
      || firstComparison.summary.total != 2) {
    std::fprintf(stderr, "missing validation comparison by id\n");
    return 1;
  }

  zs::ValidationSuiteReport lastRun{};
  if (!canaryService.last_report("demo", &lastRun) || lastRun.suite != "demo") {
    std::fprintf(stderr, "missing last canary report\n");
    return 1;
  }

  if (!canaryService.promote_last_run_to_baseline("demo", "demo-promoted")
      || !canaryService.has_baseline("demo-promoted")) {
    std::fprintf(stderr, "failed to promote canary baseline\n");
    return 1;
  }

  zs::CanaryScenarioRunRequest promotedRequest{};
  promotedRequest.session = session;
  promotedRequest.scenarioId = "demo";
  promotedRequest.baselineId = "demo-promoted";
  promotedRequest.overrides.push_back(zs::CanaryParameterOverride{"speed", "2.5"});
  const auto promotedResult = canaryService.run_scenario(promotedRequest);
  if (!promotedResult.accepted || !promotedResult.hasComparison
      || promotedResult.comparison.summary.regressed != 0) {
    std::fprintf(stderr, "promoted baseline comparison failed\n");
    return 1;
  }

    if (!services.latest_snapshot(session, &validation) || validation.summary.total != 2) {
    std::fprintf(stderr, "missing validation snapshot\n");
    return 1;
  }

  zs::ValidationSuiteReport latestReport{};
  if (!services.latest_report(session, &latestReport) || latestReport.suite != "demo") {
    std::fprintf(stderr, "missing validation report\n");
    return 1;
  }

  zs::ValidationComparisonReport latestComparison{};
  if (!services.latest_comparison(session, &latestComparison) || latestComparison.suite != "demo") {
    std::fprintf(stderr, "missing validation comparison\n");
    return 1;
  }

  zs::CanaryScenarioRunRequest badRequest{};
  badRequest.session = session;
  badRequest.scenarioId = "demo";
  badRequest.overrides.push_back(zs::CanaryParameterOverride{"speed", "99.0"});
  const auto badResult = canaryService.run_scenario(badRequest);
  if (badResult.accepted) {
    std::fprintf(stderr, "invalid canary override unexpectedly accepted\n");
    return 1;
  }

  snapshots = services.list_snapshots(session);
  if (snapshots.size() != 3 || snapshots.back().summary.failed != 1
      || snapshots.back().reportId == snapshots.front().reportId) {
    std::fprintf(stderr, "validation history growth mismatch\n");
    return 1;
  }

  std::string badComparisonJson;
  if (!services.format_comparison(session, snapshots.back().reportId,
                                  zs::InterfaceReportFormat::json, &badComparisonJson)
      || badComparisonJson.find("\"accepted\":false") == std::string::npos) {
    std::fprintf(stderr, "missing formatted bad comparison\n");
    return 1;
  }

  if (!services.close_session(session) || services.describe_session(session, &describedSession)) {
    std::fprintf(stderr, "session lifecycle mismatch\n");
    return 1;
  }

  return 0;
}
