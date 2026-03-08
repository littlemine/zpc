#pragma once

#include <vector>

#include "zensim/execution/ValidationSchema.hpp"

namespace zs {

  enum class ValidationDiffStatus : u8 { unchanged, improved, regressed, added, removed };

  inline const char *validation_diff_status_name(ValidationDiffStatus status) noexcept {
    switch (status) {
      case ValidationDiffStatus::unchanged:
        return "unchanged";
      case ValidationDiffStatus::improved:
        return "improved";
      case ValidationDiffStatus::regressed:
        return "regressed";
      case ValidationDiffStatus::added:
        return "added";
      case ValidationDiffStatus::removed:
        return "removed";
      default:
        return "unknown";
    }
  }

  struct ValidationMeasurementDiff {
    SmallString name{};
    SmallString unit{};
    bool hasBaseline{false};
    bool hasCurrent{false};
    double baselineValue{0.0};
    double currentValue{0.0};
    double delta{0.0};
    bool baselineAccepted{false};
    bool currentAccepted{false};
    ValidationDiffStatus status{ValidationDiffStatus::unchanged};
  };

  struct ValidationRecordDiff {
    SmallString recordId{};
    SmallString suite{};
    SmallString name{};
    SmallString backend{};
    SmallString executor{};
    SmallString target{};
    ValidationRecordKind kind{ValidationRecordKind::validation};
    bool hasBaseline{false};
    bool hasCurrent{false};
    ValidationOutcome baselineOutcome{ValidationOutcome::skip};
    ValidationOutcome currentOutcome{ValidationOutcome::skip};
    ValidationDiffStatus status{ValidationDiffStatus::unchanged};
    std::vector<ValidationMeasurementDiff> measurements{};
  };

  struct ValidationComparisonSummary {
    size_t total{0};
    size_t unchanged{0};
    size_t improved{0};
    size_t regressed{0};
    size_t added{0};
    size_t removed{0};

    void observe(ValidationDiffStatus status) noexcept {
      ++total;
      switch (status) {
        case ValidationDiffStatus::unchanged:
          ++unchanged;
          break;
        case ValidationDiffStatus::improved:
          ++improved;
          break;
        case ValidationDiffStatus::regressed:
          ++regressed;
          break;
        case ValidationDiffStatus::added:
          ++added;
          break;
        case ValidationDiffStatus::removed:
          ++removed;
          break;
        default:
          ++unchanged;
          break;
      }
    }
  };

  struct ValidationComparisonReport {
    SmallString suite{};
    std::vector<ValidationRecordDiff> records{};
    ValidationComparisonSummary summary{};
    bool accepted{true};
  };

  inline bool validation_record_identity_equal(const ValidationRecord &left,
                                               const ValidationRecord &right) noexcept {
    if (left.has_stable_id() && right.has_stable_id()) return left.recordId == right.recordId;
    return left.suite == right.suite && left.name == right.name && left.backend == right.backend
        && left.executor == right.executor && left.target == right.target
        && left.kind == right.kind;
  }

  inline bool validation_measurement_identity_equal(const ValidationMeasurement &left,
                                                    const ValidationMeasurement &right) noexcept {
    return left.name == right.name && left.unit == right.unit;
  }

  inline int validation_outcome_rank(ValidationOutcome outcome) noexcept {
    switch (outcome) {
      case ValidationOutcome::error:
        return 0;
      case ValidationOutcome::fail:
        return 1;
      case ValidationOutcome::skip:
        return 2;
      case ValidationOutcome::pass:
        return 3;
      default:
        return -1;
    }
  }

  inline ValidationDiffStatus compare_outcome_progress(ValidationOutcome baseline,
                                                       ValidationOutcome current) noexcept {
    const int before = validation_outcome_rank(baseline);
    const int after = validation_outcome_rank(current);
    if (after > before) return ValidationDiffStatus::improved;
    if (after < before) return ValidationDiffStatus::regressed;
    return ValidationDiffStatus::unchanged;
  }

  inline ValidationMeasurementDiff compare_validation_measurement(
      const ValidationMeasurement *baseline, const ValidationMeasurement *current) noexcept {
    ValidationMeasurementDiff diff{};
    if (baseline) {
      diff.name = baseline->name;
      diff.unit = baseline->unit;
      diff.hasBaseline = true;
      diff.baselineValue = baseline->value;
      diff.baselineAccepted = baseline->accepted();
    }
    if (current) {
      diff.name = current->name;
      diff.unit = current->unit;
      diff.hasCurrent = true;
      diff.currentValue = current->value;
      diff.currentAccepted = current->accepted();
    }
    diff.delta = diff.currentValue - diff.baselineValue;

    if (baseline && current) {
      if (diff.currentAccepted && !diff.baselineAccepted)
        diff.status = ValidationDiffStatus::improved;
      else if (!diff.currentAccepted && diff.baselineAccepted)
        diff.status = ValidationDiffStatus::regressed;
      else
        diff.status = ValidationDiffStatus::unchanged;
    } else if (current) {
      diff.status = ValidationDiffStatus::added;
    } else if (baseline) {
      diff.status = ValidationDiffStatus::removed;
    }
    return diff;
  }

  inline ValidationRecordDiff compare_validation_record(const ValidationRecord *baseline,
                                                        const ValidationRecord *current) {
    ValidationRecordDiff diff{};
    if (baseline) {
      diff.recordId = baseline->recordId;
      diff.suite = baseline->suite;
      diff.name = baseline->name;
      diff.backend = baseline->backend;
      diff.executor = baseline->executor;
      diff.target = baseline->target;
      diff.kind = baseline->kind;
      diff.hasBaseline = true;
      diff.baselineOutcome = baseline->outcome;
    }
    if (current) {
      diff.recordId = current->recordId;
      diff.suite = current->suite;
      diff.name = current->name;
      diff.backend = current->backend;
      diff.executor = current->executor;
      diff.target = current->target;
      diff.kind = current->kind;
      diff.hasCurrent = true;
      diff.currentOutcome = current->outcome;
    }

    if (baseline && current) {
      diff.status = compare_outcome_progress(baseline->outcome, current->outcome);

      std::vector<bool> matched(current->measurements.size(), false);
      for (const auto &baselineMeasurement : baseline->measurements) {
        const ValidationMeasurement *currentMeasurement = nullptr;
        for (size_t i = 0; i < current->measurements.size(); ++i) {
          if (!matched[i]
              && validation_measurement_identity_equal(baselineMeasurement,
                                                       current->measurements[i])) {
            matched[i] = true;
            currentMeasurement = &current->measurements[i];
            break;
          }
        }
        auto measurementDiff = compare_validation_measurement(&baselineMeasurement, currentMeasurement);
        if (measurementDiff.status == ValidationDiffStatus::regressed)
          diff.status = ValidationDiffStatus::regressed;
        else if (measurementDiff.status == ValidationDiffStatus::improved
                 && diff.status == ValidationDiffStatus::unchanged)
          diff.status = ValidationDiffStatus::improved;
        diff.measurements.push_back(zs::move(measurementDiff));
      }

      for (size_t i = 0; i < current->measurements.size(); ++i) {
        if (!matched[i]) diff.measurements.push_back(compare_validation_measurement(nullptr, &current->measurements[i]));
      }
    } else if (current) {
      diff.status = ValidationDiffStatus::added;
      for (const auto &measurement : current->measurements)
        diff.measurements.push_back(compare_validation_measurement(nullptr, &measurement));
    } else if (baseline) {
      diff.status = ValidationDiffStatus::removed;
      for (const auto &measurement : baseline->measurements)
        diff.measurements.push_back(compare_validation_measurement(&measurement, nullptr));
    }

    return diff;
  }

  inline ValidationComparisonReport compare_validation_reports(const ValidationSuiteReport &baseline,
                                                               const ValidationSuiteReport &current) {
    ValidationComparisonReport report{};
    report.suite = current.suite.size() ? current.suite : baseline.suite;

    std::vector<bool> matched(current.records.size(), false);
    for (const auto &baselineRecord : baseline.records) {
      const ValidationRecord *currentRecord = nullptr;
      for (size_t i = 0; i < current.records.size(); ++i) {
        if (!matched[i] && validation_record_identity_equal(baselineRecord, current.records[i])) {
          matched[i] = true;
          currentRecord = &current.records[i];
          break;
        }
      }
      auto diff = compare_validation_record(&baselineRecord, currentRecord);
      report.summary.observe(diff.status);
      if (diff.status == ValidationDiffStatus::regressed) report.accepted = false;
      report.records.push_back(zs::move(diff));
    }

    for (size_t i = 0; i < current.records.size(); ++i) {
      if (!matched[i]) {
        auto diff = compare_validation_record(nullptr, &current.records[i]);
        report.summary.observe(diff.status);
        report.records.push_back(zs::move(diff));
      }
    }

    return report;
  }

}  // namespace zs