#pragma once

#include <mutex>
#include <vector>

#include "zensim/interface/CanaryScenario.hpp"
#include "zensim/interface/LocalInterfaceServices.hpp"

namespace zs {

  class LocalCanaryScenarioService final : public CanaryScenarioService {
  public:
    explicit LocalCanaryScenarioService(LocalInterfaceServices &services) : _services{services} {}

    void register_scenario(CanaryScenarioDescriptor descriptor) {
      if (descriptor.scenarioId.size() == 0) return;
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto &existing : _scenarios) {
        if (existing.scenarioId == descriptor.scenarioId) {
          existing = zs::move(descriptor);
          return;
        }
      }
      _scenarios.push_back(zs::move(descriptor));
    }

    std::vector<CanaryScenarioDescriptor> list_scenarios(
        InterfaceSessionHandle session) const override {
      if (!_services.session_exists(session)) return {};
      std::lock_guard<std::mutex> lock(_mutex);
      return _scenarios;
    }

    bool describe_scenario(InterfaceSessionHandle session, const SmallString &scenarioId,
                           CanaryScenarioDescriptor *descriptor) const override {
      if (!_services.session_exists(session) || descriptor == nullptr || scenarioId.size() == 0)
        return false;
      std::lock_guard<std::mutex> lock(_mutex);
      for (const auto &scenario : _scenarios) {
        if (scenario.scenarioId == scenarioId) {
          *descriptor = scenario;
          return true;
        }
      }
      return false;
    }

    CanaryScenarioRunResult run_scenario(const CanaryScenarioRunRequest &request) override {
      CanaryScenarioRunResult result{};
      result.scenarioId = request.scenarioId;
      result.report.suite = request.scenarioId;

      CanaryScenarioDescriptor scenario{};
      if (!_services.session_exists(request.session)
          || !describe_scenario(request.session, request.scenarioId, &scenario)) {
        ValidationRecord record{};
        record.recordId = "canary.missing";
        record.suite = request.scenarioId;
        record.name = "scenario_available";
        record.kind = ValidationRecordKind::validation;
        record.outcome = ValidationOutcome::fail;
        result.report.records.push_back(zs::move(record));
        result.report.refresh_summary();
        result.accepted = false;
        _services.publish_validation(request.session, result.report);
        return result;
      }

      ValidationRecord record{};
      record.recordId = scenario.scenarioId;
      record.suite = scenario.scenarioId;
      record.name = scenario.label.size() ? scenario.label : scenario.scenarioId;
      record.kind = ValidationRecordKind::benchmark;
      record.outcome = ValidationOutcome::pass;
      record.note = "local_canary";

      ValidationMeasurement parameterCount{};
      parameterCount.name = "override_count";
      parameterCount.unit = "count";
      parameterCount.value = static_cast<double>(request.overrides.size());
      record.measurements.push_back(zs::move(parameterCount));

      ValidationMeasurement declaredParameters{};
      declaredParameters.name = "declared_parameters";
      declaredParameters.unit = "count";
      declaredParameters.value = static_cast<double>(scenario.parameters.size());
      record.measurements.push_back(zs::move(declaredParameters));

      result.report.records.push_back(zs::move(record));
      result.report.refresh_summary();
      result.accepted = result.report.summary.failed == 0 && result.report.summary.errored == 0;
      _services.publish_validation(request.session, result.report);
      return result;
    }

  private:
    LocalInterfaceServices &_services;
    mutable std::mutex _mutex{};
    std::vector<CanaryScenarioDescriptor> _scenarios{};
  };

}  // namespace zs