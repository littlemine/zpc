#include <cstdio>

#include "zensim/interface/LocalCanaryService.hpp"

int main() {
  zs::AsyncRuntime runtime{1};
  zs::AsyncResourceManager resourceManager{runtime};
  zs::LocalInterfaceServices services{runtime, &resourceManager};
  zs::LocalCanaryScenarioService canaryService{services};

  zs::InterfaceServiceBundle bundle{&services, &services, &services, &services};
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

  zs::AsyncSubmission submission{};
  submission.executor = "inline";
  submission.desc.label = "noop";
  submission.step = [](zs::AsyncExecutionContext &) { return zs::AsyncPollStatus::completed; };
  const auto handle = services.submit(session, zs::move(submission));
  zs::InterfaceSubmissionSummary submissionSummary{};
  if (!handle.valid() || !services.query_submission(session, handle.id(), &submissionSummary)
      || submissionSummary.status != zs::AsyncTaskStatus::completed) {
    std::fprintf(stderr, "submission flow failed\n");
    return 1;
  }

  zs::CanaryScenarioDescriptor scenario{};
  scenario.scenarioId = "demo";
  scenario.label = "Demo";
  scenario.parameters.push_back(zs::CanaryParameterDescriptor{"speed", "Speed"});
  canaryService.register_scenario(zs::move(scenario));

  zs::InterfaceValidationSnapshot validation{};
  const auto scenarios = canaryService.list_scenarios(session);
  if (scenarios.empty() || scenarios.front().scenarioId != "demo") {
    std::fprintf(stderr, "missing canary scenario\n");
    return 1;
  }

  zs::CanaryScenarioRunRequest request{};
  request.session = session;
  request.scenarioId = scenarios.front().scenarioId;
  const auto result = canaryService.run_scenario(request);
  if (!result.accepted) {
    std::fprintf(stderr, "canary scenario rejected\n");
    return 1;
  }

  if (!services.latest_snapshot(session, &validation) || validation.summary.total != 1) {
    std::fprintf(stderr, "missing validation snapshot\n");
    return 1;
  }

  zs::ValidationSuiteReport latestReport{};
  if (!services.latest_report(session, &latestReport) || latestReport.suite != "demo") {
    std::fprintf(stderr, "missing validation report\n");
    return 1;
  }

  return 0;
}