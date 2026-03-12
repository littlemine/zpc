#pragma once

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "zensim/interface/CanaryScenario.hpp"
#include "zensim/interface/LocalInterfaceServices.hpp"
#include "zensim/execution/ValidationPersistence.hpp"

namespace zs {

  class LocalCanaryScenarioService final : public CanaryScenarioService {
  public:
    explicit LocalCanaryScenarioService(LocalInterfaceServices &services) : _services{services} {}

    void register_scenario(CanaryScenarioDescriptor descriptor) {
      if (descriptor.scenarioId.size() == 0) return;
      std::lock_guard<Mutex> lock(_mutex);
      for (auto &existing : _scenarios) {
        if (existing.scenarioId == descriptor.scenarioId) {
          existing = zs::move(descriptor);
          return;
        }
      }
      _scenarios.push_back(zs::move(descriptor));
    }

    void register_baseline(const SmallString &baselineId, ValidationSuiteReport report) {
      if (baselineId.size() == 0) return;
      report.refresh_summary();
      std::lock_guard<Mutex> lock(_mutex);
      _baselines.insert_or_assign(baselineId.asChars(), zs::move(report));
    }

    bool has_baseline(const SmallString &baselineId) const {
      if (baselineId.size() == 0) return false;
      std::lock_guard<Mutex> lock(_mutex);
      return _baselines.find(baselineId.asChars()) != _baselines.end();
    }

    bool last_report(const SmallString &scenarioId, ValidationSuiteReport *report) const {
      if (scenarioId.size() == 0 || report == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto it = _lastReports.find(scenarioId.asChars());
      if (it == _lastReports.end()) return false;
      *report = it->second;
      return true;
    }

    bool promote_last_run_to_baseline(const SmallString &scenarioId, const SmallString &baselineId) {
      if (scenarioId.size() == 0 || baselineId.size() == 0) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto it = _lastReports.find(scenarioId.asChars());
      if (it == _lastReports.end()) return false;
      _baselines.insert_or_assign(baselineId.asChars(), it->second);
      return true;
    }

    bool save_baseline(const SmallString &baselineId, const std::string &path,
                       const SmallString &scenarioId = {}, const SmallString &note = {},
                       std::string *errorMessage = nullptr) {
      std::lock_guard<Mutex> lock(_mutex);
      auto it = _baselines.find(baselineId.asChars());
      if (it == _baselines.end()) {
        detail::set_validation_persistence_error(errorMessage, "baseline not found");
        return false;
      }
      if (!save_validation_report_json_file(it->second, path, errorMessage)) return false;
      CanaryBaselineProvenance provenance{};
      provenance.baselineId = baselineId;
      provenance.scenarioId = scenarioId;
      provenance.note = note;
      provenance.timestamp = current_timestamp_();
      _provenance.insert_or_assign(baselineId.asChars(), zs::move(provenance));
      return true;
    }

    bool load_baseline(const SmallString &baselineId, const std::string &path,
                       const SmallString &scenarioId = {}, const SmallString &note = {},
                       std::string *errorMessage = nullptr) {
      if (baselineId.size() == 0) {
        detail::set_validation_persistence_error(errorMessage, "baselineId must not be empty");
        return false;
      }
      ValidationSuiteReport report{};
      if (!load_validation_report_json_file(path, &report, errorMessage)) return false;
      std::lock_guard<Mutex> lock(_mutex);
      _baselines.insert_or_assign(baselineId.asChars(), zs::move(report));
      CanaryBaselineProvenance provenance{};
      provenance.baselineId = baselineId;
      provenance.scenarioId = scenarioId;
      provenance.note = note;
      provenance.timestamp = current_timestamp_();
      _provenance.insert_or_assign(baselineId.asChars(), zs::move(provenance));
      return true;
    }

    std::vector<CanaryBaselineProvenance> list_baselines() const {
      std::lock_guard<Mutex> lock(_mutex);
      std::vector<CanaryBaselineProvenance> result;
      result.reserve(_baselines.size());
      for (const auto &entry : _baselines) {
        auto it = _provenance.find(entry.first);
        if (it != _provenance.end()) {
          result.push_back(it->second);
        } else {
          CanaryBaselineProvenance provenance{};
          provenance.baselineId = SmallString{entry.first.c_str()};
          result.push_back(zs::move(provenance));
        }
      }
      return result;
    }

    bool describe_baseline(const SmallString &baselineId,
                           CanaryBaselineProvenance *provenance) const {
      if (baselineId.size() == 0 || provenance == nullptr) return false;
      std::lock_guard<Mutex> lock(_mutex);
      auto it = _provenance.find(baselineId.asChars());
      if (it == _provenance.end()) {
        if (_baselines.find(baselineId.asChars()) == _baselines.end()) return false;
        provenance->baselineId = baselineId;
        provenance->scenarioId = {};
        provenance->note = {};
        provenance->timestamp = 0;
        return true;
      }
      *provenance = it->second;
      return true;
    }

    std::vector<CanaryScenarioDescriptor> list_scenarios(
        InterfaceSessionHandle session) const override {
      if (!_services.session_exists(session)) return {};
      std::lock_guard<Mutex> lock(_mutex);
      return _scenarios;
    }

    bool describe_scenario(InterfaceSessionHandle session, const SmallString &scenarioId,
                           CanaryScenarioDescriptor *descriptor) const override {
      if (!_services.session_exists(session) || descriptor == nullptr || scenarioId.size() == 0)
        return false;
      std::lock_guard<Mutex> lock(_mutex);
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

      ValidationRecord schemaRecord{};
      schemaRecord.recordId = scenario.scenarioId;
      schemaRecord.suite = scenario.scenarioId;
      schemaRecord.name = "scenario_schema";
      schemaRecord.kind = ValidationRecordKind::validation;
      schemaRecord.outcome = ValidationOutcome::pass;
      schemaRecord.note = "local_canary_schema";

      bool accepted = true;
      for (const auto &parameter : scenario.parameters) {
        const auto *overrideValue = find_override_(request.overrides, parameter.name);
        const auto *effectiveValue = overrideValue != nullptr ? overrideValue : default_or_null_(parameter);

        ValidationMeasurement measurement{};
        measurement.name = parameter.name;
        measurement.unit = parameter.unit;
        measurement.value = effectiveValue != nullptr ? 1.0 : 0.0;
        if (parameter.required && effectiveValue == nullptr) {
          schemaRecord.outcome = ValidationOutcome::fail;
          accepted = false;
        } else if (effectiveValue != nullptr && !validate_value_(parameter, *effectiveValue)) {
          schemaRecord.outcome = ValidationOutcome::fail;
          accepted = false;
        }
        schemaRecord.measurements.push_back(zs::move(measurement));
      }

      for (const auto &overrideValue : request.overrides) {
        if (!has_parameter_(scenario, overrideValue.name)) {
          schemaRecord.outcome = ValidationOutcome::fail;
          accepted = false;
        }
      }

      result.report.records.push_back(zs::move(schemaRecord));

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
      result.accepted = accepted && result.report.summary.failed == 0 && result.report.summary.errored == 0;

      {
        std::lock_guard<Mutex> lock(_mutex);
        _lastReports.insert_or_assign(result.scenarioId.asChars(), result.report);
      }

      const auto baselineKey = request.baselineId.size() ? request.baselineId : scenario.scenarioId;
      ValidationComparisonReport comparison{};
      const auto *baseline = find_baseline_(baselineKey);
      if (request.compareAgainstBaseline && baseline != nullptr) {
        comparison = compare_validation_reports(*baseline, result.report);
        result.comparison = comparison;
        result.hasComparison = true;
        result.accepted = result.accepted && comparison.accepted;
        _services.publish_validation(request.session, result.report, &comparison);
      } else {
        _services.publish_validation(request.session, result.report);
      }
      return result;
    }

  private:
    static const SmallString *find_override_(const std::vector<CanaryParameterOverride> &overrides,
                                             const SmallString &name) {
      for (const auto &overrideValue : overrides)
        if (overrideValue.name == name) return &overrideValue.value;
      return nullptr;
    }

    static const SmallString *default_or_null_(const CanaryParameterDescriptor &parameter) {
      return parameter.defaultValue.size() != 0 ? &parameter.defaultValue : nullptr;
    }

    static bool has_parameter_(const CanaryScenarioDescriptor &scenario, const SmallString &name) {
      for (const auto &parameter : scenario.parameters)
        if (parameter.name == name) return true;
      return false;
    }

    static bool parse_integer_(const SmallString &value, long long *parsed) {
      if (parsed == nullptr || value.size() == 0) return false;
      char *end = nullptr;
      errno = 0;
      const auto result = std::strtoll(value.asChars(), &end, 10);
      if (errno != 0 || end == value.asChars() || *end != '\0') return false;
      *parsed = result;
      return true;
    }

    static bool parse_double_(const SmallString &value, double *parsed) {
      if (parsed == nullptr || value.size() == 0) return false;
      char *end = nullptr;
      errno = 0;
      const auto result = std::strtod(value.asChars(), &end);
      if (errno != 0 || end == value.asChars() || *end != '\0') return false;
      *parsed = result;
      return true;
    }

    static bool validate_value_(const CanaryParameterDescriptor &parameter, const SmallString &value) {
      switch (parameter.kind) {
        case CanaryParameterKind::boolean:
          return value == "true" || value == "false" || value == "1" || value == "0";
        case CanaryParameterKind::integer: {
          long long parsed = 0;
          if (!parse_integer_(value, &parsed)) return false;
          long long minimum = 0;
          long long maximum = 0;
          if (parameter.minValue.size() != 0 && parse_integer_(parameter.minValue, &minimum)
              && parsed < minimum)
            return false;
          if (parameter.maxValue.size() != 0 && parse_integer_(parameter.maxValue, &maximum)
              && parsed > maximum)
            return false;
          return true;
        }
        case CanaryParameterKind::floating_point: {
          double parsed = 0.0;
          if (!parse_double_(value, &parsed)) return false;
          double minimum = 0.0;
          double maximum = 0.0;
          if (parameter.minValue.size() != 0 && parse_double_(parameter.minValue, &minimum)
              && parsed < minimum)
            return false;
          if (parameter.maxValue.size() != 0 && parse_double_(parameter.maxValue, &maximum)
              && parsed > maximum)
            return false;
          return true;
        }
        case CanaryParameterKind::enumeration:
          for (const auto &option : parameter.options)
            if (option.value == value) return true;
          return false;
        case CanaryParameterKind::string:
        default:
          return true;
      }
    }

    const ValidationSuiteReport *find_baseline_(const SmallString &baselineId) const {
      std::lock_guard<Mutex> lock(_mutex);
      auto it = _baselines.find(baselineId.asChars());
      return it == _baselines.end() ? nullptr : &it->second;
    }

    static u64 current_timestamp_() noexcept {
      const auto now = std::chrono::steady_clock::now();
      return static_cast<u64>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }

    LocalInterfaceServices &_services;
    mutable Mutex _mutex{};
    std::vector<CanaryScenarioDescriptor> _scenarios{};
    std::unordered_map<std::string, ValidationSuiteReport> _baselines{};
    std::unordered_map<std::string, ValidationSuiteReport> _lastReports{};
    std::unordered_map<std::string, CanaryBaselineProvenance> _provenance{};
  };

}  // namespace zs