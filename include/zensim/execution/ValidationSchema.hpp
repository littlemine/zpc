#pragma once

#include <vector>

#include "zensim/TypeAlias.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

  enum class ValidationRecordKind : u8 { validation, benchmark };
  enum class ValidationOutcome : u8 { pass, fail, skip, error };
  enum class ValidationThresholdMode : u8 { none, less_equal, greater_equal, inclusive_range };

  inline bool is_success(ValidationOutcome outcome) noexcept {
    return outcome == ValidationOutcome::pass || outcome == ValidationOutcome::skip;
  }

  struct ValidationThreshold {
    ValidationThresholdMode mode{ValidationThresholdMode::none};
    double reference{0.0};
    double tolerance{0.0};
    double lowerBound{0.0};
    double upperBound{0.0};

    bool satisfied_by(double value) const noexcept {
      switch (mode) {
        case ValidationThresholdMode::none:
          return true;
        case ValidationThresholdMode::less_equal:
          return value <= reference + tolerance;
        case ValidationThresholdMode::greater_equal:
          return value + tolerance >= reference;
        case ValidationThresholdMode::inclusive_range:
          return value >= lowerBound && value <= upperBound;
        default:
          return false;
      }
    }
  };

  struct ValidationMeasurement {
    SmallString name{};
    SmallString unit{};
    double value{0.0};
    ValidationThreshold threshold{};

    bool accepted() const noexcept { return threshold.satisfied_by(value); }
  };

  struct ValidationRecord {
    SmallString recordId{};
    SmallString suite{};
    SmallString name{};
    SmallString backend{};
    SmallString executor{};
    SmallString target{};
    SmallString note{};
    ValidationRecordKind kind{ValidationRecordKind::validation};
    ValidationOutcome outcome{ValidationOutcome::pass};
    u64 durationNs{0};
    std::vector<ValidationMeasurement> measurements{};

    bool accepted() const noexcept {
      if (!is_success(outcome)) return false;
      for (const auto &measurement : measurements)
        if (!measurement.accepted()) return false;
      return true;
    }

    bool has_stable_id() const noexcept { return recordId.size() != 0; }
  };

  struct ValidationSummary {
    size_t total{0};
    size_t passed{0};
    size_t failed{0};
    size_t skipped{0};
    size_t errored{0};

    void clear() noexcept { *this = {}; }

    void observe(const ValidationRecord &record) noexcept {
      ++total;
      switch (record.outcome) {
        case ValidationOutcome::pass:
          ++passed;
          break;
        case ValidationOutcome::fail:
          ++failed;
          break;
        case ValidationOutcome::skip:
          ++skipped;
          break;
        case ValidationOutcome::error:
          ++errored;
          break;
        default:
          ++errored;
          break;
      }
    }
  };

  struct ValidationSuiteReport {
    SmallString schemaVersion{"zpc.validation.v1"};
    SmallString suite{};
    std::vector<ValidationRecord> records{};
    ValidationSummary summary{};

    void refresh_summary() noexcept {
      summary.clear();
      for (const auto &record : records) summary.observe(record);
    }
  };

}  // namespace zs