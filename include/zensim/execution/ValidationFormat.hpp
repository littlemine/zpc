#pragma once

#include <sstream>
#include <string>

#include "zensim/execution/ValidationCompare.hpp"
#include "zensim/execution/ValidationSchema.hpp"

namespace zs {

  inline const char *validation_record_kind_name(ValidationRecordKind kind) noexcept {
    switch (kind) {
      case ValidationRecordKind::validation:
        return "validation";
      case ValidationRecordKind::benchmark:
        return "benchmark";
      default:
        return "unknown";
    }
  }

  inline const char *validation_outcome_name(ValidationOutcome outcome) noexcept {
    switch (outcome) {
      case ValidationOutcome::pass:
        return "pass";
      case ValidationOutcome::fail:
        return "fail";
      case ValidationOutcome::skip:
        return "skip";
      case ValidationOutcome::error:
        return "error";
      default:
        return "unknown";
    }
  }

  inline const char *validation_threshold_mode_name(ValidationThresholdMode mode) noexcept {
    switch (mode) {
      case ValidationThresholdMode::none:
        return "none";
      case ValidationThresholdMode::less_equal:
        return "less_equal";
      case ValidationThresholdMode::greater_equal:
        return "greater_equal";
      case ValidationThresholdMode::inclusive_range:
        return "inclusive_range";
      default:
        return "unknown";
    }
  }

  inline void append_json_escaped(std::ostringstream &oss, const char *text) {
    if (!text) return;
    for (; *text; ++text) {
      switch (*text) {
        case '\\':
          oss << "\\\\";
          break;
        case '"':
          oss << "\\\"";
          break;
        case '\n':
          oss << "\\n";
          break;
        case '\r':
          oss << "\\r";
          break;
        case '\t':
          oss << "\\t";
          break;
        default:
          oss << *text;
          break;
      }
    }
  }

  inline void append_json_string_field(std::ostringstream &oss, const char *name,
                                       const SmallString &value) {
    oss << '"' << name << "\":\"";
    append_json_escaped(oss, value.asChars());
    oss << '"';
  }

  inline void append_validation_metadata_json(std::ostringstream &oss,
                                              const std::vector<ValidationMetadataEntry> &metadata) {
    oss << '"' << "metadata" << "\":[";
    for (size_t i = 0; i < metadata.size(); ++i) {
      if (i) oss << ',';
      oss << '{';
      append_json_string_field(oss, "key", metadata[i].key);
      oss << ',';
      append_json_string_field(oss, "value", metadata[i].value);
      oss << '}';
    }
    oss << ']';
  }

  inline std::string format_validation_record_json(const ValidationRecord &record) {
    std::ostringstream oss;
    oss << '{';
    append_json_string_field(oss, "recordId", record.recordId);
    oss << ',';
    append_json_string_field(oss, "suite", record.suite);
    oss << ',';
    append_json_string_field(oss, "name", record.name);
    oss << ',';
    append_json_string_field(oss, "backend", record.backend);
    oss << ',';
    append_json_string_field(oss, "executor", record.executor);
    oss << ',';
    append_json_string_field(oss, "target", record.target);
    oss << ',';
    append_json_string_field(oss, "note", record.note);
    oss << ",\"kind\":\"" << validation_record_kind_name(record.kind) << '"';
    oss << ",\"outcome\":\"" << validation_outcome_name(record.outcome) << '"';
    oss << ",\"durationNs\":" << record.durationNs;
    oss << ",\"accepted\":" << (record.accepted() ? "true" : "false");
    oss << ',';
    append_validation_metadata_json(oss, record.metadata);
    oss << ",\"measurements\":[";
    for (size_t i = 0; i < record.measurements.size(); ++i) {
      if (i) oss << ',';
      const auto &measurement = record.measurements[i];
      oss << '{';
      append_json_string_field(oss, "name", measurement.name);
      oss << ',';
      append_json_string_field(oss, "unit", measurement.unit);
      oss << ",\"value\":" << measurement.value;
      oss << ",\"accepted\":" << (measurement.accepted() ? "true" : "false");
      oss << ",\"threshold\":{";
      oss << "\"mode\":\"" << validation_threshold_mode_name(measurement.threshold.mode)
          << '"';
      oss << ",\"reference\":" << measurement.threshold.reference;
      oss << ",\"tolerance\":" << measurement.threshold.tolerance;
      oss << ",\"lowerBound\":" << measurement.threshold.lowerBound;
      oss << ",\"upperBound\":" << measurement.threshold.upperBound;
      oss << "}}";
    }
    oss << "]}";
    return oss.str();
  }

  inline std::string format_validation_report_json(ValidationSuiteReport report) {
    report.refresh_summary();

    std::ostringstream oss;
    oss << '{';
    append_json_string_field(oss, "schemaVersion", report.schemaVersion);
    oss << ',';
    append_json_string_field(oss, "suite", report.suite);
    oss << ',';
    append_validation_metadata_json(oss, report.metadata);
    oss << ",\"summary\":{";
    oss << "\"total\":" << report.summary.total;
    oss << ",\"passed\":" << report.summary.passed;
    oss << ",\"failed\":" << report.summary.failed;
    oss << ",\"skipped\":" << report.summary.skipped;
    oss << ",\"errored\":" << report.summary.errored;
    oss << "},\"records\":[";
    for (size_t i = 0; i < report.records.size(); ++i) {
      if (i) oss << ',';
      oss << format_validation_record_json(report.records[i]);
    }
    oss << "]}";
    return oss.str();
  }

  inline std::string format_validation_summary_text(ValidationSuiteReport report) {
    report.refresh_summary();

    std::ostringstream oss;
    oss << "suite=" << report.suite.asChars();
    oss << " schema=" << report.schemaVersion.asChars();
    oss << " total=" << report.summary.total;
    oss << " passed=" << report.summary.passed;
    oss << " failed=" << report.summary.failed;
    oss << " skipped=" << report.summary.skipped;
    oss << " errored=" << report.summary.errored;

    for (const auto &record : report.records) {
      oss << "\n- [" << validation_outcome_name(record.outcome) << "] ";
      oss << record.name.asChars();
      if (record.recordId.size()) oss << " recordId=" << record.recordId.asChars();
      if (record.backend.size()) oss << " backend=" << record.backend.asChars();
      if (record.executor.size()) oss << " executor=" << record.executor.asChars();
      if (record.target.size()) oss << " target=" << record.target.asChars();
      oss << " kind=" << validation_record_kind_name(record.kind);
      oss << " accepted=" << (record.accepted() ? "true" : "false");
      if (record.durationNs) oss << " durationNs=" << record.durationNs;
      for (const auto &metadata : record.metadata)
        oss << " metadata." << metadata.key.asChars() << '=' << metadata.value.asChars();
    }
    return oss.str();
  }

  inline std::string format_validation_report_text(ValidationSuiteReport report) {
    report.refresh_summary();

    std::ostringstream oss;
    oss << format_validation_summary_text(report);

    for (const auto &metadata : report.metadata)
      oss << "\nmetadata." << metadata.key.asChars() << '=' << metadata.value.asChars();

    for (const auto &record : report.records) {
      if (record.note.size()) oss << "\n  note=" << record.note.asChars();
      for (const auto &metadata : record.metadata)
        oss << "\n  metadata." << metadata.key.asChars() << '=' << metadata.value.asChars();
      for (const auto &measurement : record.measurements) {
        oss << "\n  * " << measurement.name.asChars() << '=' << measurement.value;
        if (measurement.unit.size()) oss << ' ' << measurement.unit.asChars();
        oss << " accepted=" << (measurement.accepted() ? "true" : "false");
        oss << " threshold.mode="
            << validation_threshold_mode_name(measurement.threshold.mode);
        switch (measurement.threshold.mode) {
          case ValidationThresholdMode::less_equal:
          case ValidationThresholdMode::greater_equal:
            oss << " threshold.reference=" << measurement.threshold.reference;
            oss << " threshold.tolerance=" << measurement.threshold.tolerance;
            break;
          case ValidationThresholdMode::inclusive_range:
            oss << " threshold.lowerBound=" << measurement.threshold.lowerBound;
            oss << " threshold.upperBound=" << measurement.threshold.upperBound;
            break;
          default:
            break;
        }
      }
    }
    return oss.str();
  }

  inline std::string format_validation_measurement_diff_json(
      const ValidationMeasurementDiff &measurement) {
    std::ostringstream oss;
    oss << '{';
    append_json_string_field(oss, "name", measurement.name);
    oss << ',';
    append_json_string_field(oss, "unit", measurement.unit);
    oss << ",\"hasBaseline\":" << (measurement.hasBaseline ? "true" : "false");
    oss << ",\"hasCurrent\":" << (measurement.hasCurrent ? "true" : "false");
    oss << ",\"baselineValue\":" << measurement.baselineValue;
    oss << ",\"currentValue\":" << measurement.currentValue;
    oss << ",\"delta\":" << measurement.delta;
    oss << ",\"baselineAccepted\":" << (measurement.baselineAccepted ? "true" : "false");
    oss << ",\"currentAccepted\":" << (measurement.currentAccepted ? "true" : "false");
    oss << ",\"status\":\"" << validation_diff_status_name(measurement.status) << '"';
    oss << '}';
    return oss.str();
  }

  inline std::string format_validation_record_diff_json(const ValidationRecordDiff &record) {
    std::ostringstream oss;
    oss << '{';
    append_json_string_field(oss, "recordId", record.recordId);
    oss << ',';
    append_json_string_field(oss, "suite", record.suite);
    oss << ',';
    append_json_string_field(oss, "name", record.name);
    oss << ',';
    append_json_string_field(oss, "backend", record.backend);
    oss << ',';
    append_json_string_field(oss, "executor", record.executor);
    oss << ',';
    append_json_string_field(oss, "target", record.target);
    oss << ",\"kind\":\"" << validation_record_kind_name(record.kind) << '"';
    oss << ",\"hasBaseline\":" << (record.hasBaseline ? "true" : "false");
    oss << ",\"hasCurrent\":" << (record.hasCurrent ? "true" : "false");
    oss << ",\"baselineOutcome\":\"" << validation_outcome_name(record.baselineOutcome) << '"';
    oss << ",\"currentOutcome\":\"" << validation_outcome_name(record.currentOutcome) << '"';
    oss << ",\"status\":\"" << validation_diff_status_name(record.status) << '"';
    oss << ",\"measurements\":[";
    for (size_t i = 0; i < record.measurements.size(); ++i) {
      if (i) oss << ',';
      oss << format_validation_measurement_diff_json(record.measurements[i]);
    }
    oss << "]}";
    return oss.str();
  }

  inline std::string format_validation_comparison_report_json(
      const ValidationComparisonReport &report) {
    std::ostringstream oss;
    oss << '{';
    append_json_string_field(oss, "suite", report.suite);
    oss << ",\"accepted\":" << (report.accepted ? "true" : "false");
    oss << ",\"summary\":{";
    oss << "\"total\":" << report.summary.total;
    oss << ",\"unchanged\":" << report.summary.unchanged;
    oss << ",\"improved\":" << report.summary.improved;
    oss << ",\"regressed\":" << report.summary.regressed;
    oss << ",\"added\":" << report.summary.added;
    oss << ",\"removed\":" << report.summary.removed;
    oss << "},\"records\":[";
    for (size_t i = 0; i < report.records.size(); ++i) {
      if (i) oss << ',';
      oss << format_validation_record_diff_json(report.records[i]);
    }
    oss << "]}";
    return oss.str();
  }

  inline std::string format_validation_comparison_summary_text(
      const ValidationComparisonReport &report) {
    std::ostringstream oss;
    oss << "suite=" << report.suite.asChars();
    oss << " accepted=" << (report.accepted ? "true" : "false");
    oss << " total=" << report.summary.total;
    oss << " unchanged=" << report.summary.unchanged;
    oss << " improved=" << report.summary.improved;
    oss << " regressed=" << report.summary.regressed;
    oss << " added=" << report.summary.added;
    oss << " removed=" << report.summary.removed;

    for (const auto &record : report.records) {
      oss << "\n- [" << validation_diff_status_name(record.status) << "] ";
      oss << record.name.asChars();
      if (record.recordId.size()) oss << " recordId=" << record.recordId.asChars();
      if (record.backend.size()) oss << " backend=" << record.backend.asChars();
      if (record.executor.size()) oss << " executor=" << record.executor.asChars();
      if (record.target.size()) oss << " target=" << record.target.asChars();
      oss << " kind=" << validation_record_kind_name(record.kind);
      oss << " baselineOutcome=" << validation_outcome_name(record.baselineOutcome);
      oss << " currentOutcome=" << validation_outcome_name(record.currentOutcome);

      for (const auto &measurement : record.measurements) {
        oss << "\n  * [" << validation_diff_status_name(measurement.status) << "] ";
        oss << measurement.name.asChars();
        if (measurement.unit.size()) oss << " unit=" << measurement.unit.asChars();
        if (measurement.hasBaseline) oss << " baselineValue=" << measurement.baselineValue;
        if (measurement.hasCurrent) oss << " currentValue=" << measurement.currentValue;
        oss << " delta=" << measurement.delta;
        oss << " baselineAccepted=" << (measurement.baselineAccepted ? "true" : "false");
        oss << " currentAccepted=" << (measurement.currentAccepted ? "true" : "false");
      }
    }
    return oss.str();
  }

  inline std::string format_validation_comparison_report_text(
      const ValidationComparisonReport &report) {
    std::ostringstream oss;
    oss << "suite=" << report.suite.asChars();
    oss << " accepted=" << (report.accepted ? "true" : "false");
    oss << " total=" << report.summary.total;
    oss << " unchanged=" << report.summary.unchanged;
    oss << " improved=" << report.summary.improved;
    oss << " regressed=" << report.summary.regressed;
    oss << " added=" << report.summary.added;
    oss << " removed=" << report.summary.removed;

    for (const auto &record : report.records) {
      oss << "\n- [" << validation_diff_status_name(record.status) << "] ";
      oss << record.name.asChars();
      if (record.recordId.size()) oss << " recordId=" << record.recordId.asChars();
      if (record.backend.size()) oss << " backend=" << record.backend.asChars();
      if (record.executor.size()) oss << " executor=" << record.executor.asChars();
      if (record.target.size()) oss << " target=" << record.target.asChars();
      oss << " kind=" << validation_record_kind_name(record.kind);
      oss << " baselineOutcome=" << validation_outcome_name(record.baselineOutcome);
      oss << " currentOutcome=" << validation_outcome_name(record.currentOutcome);
      for (const auto &measurement : record.measurements) {
        oss << "\n  * [" << validation_diff_status_name(measurement.status) << "] ";
        oss << measurement.name.asChars();
        if (measurement.unit.size()) oss << " unit=" << measurement.unit.asChars();
        if (measurement.hasBaseline) oss << " baselineValue=" << measurement.baselineValue;
        if (measurement.hasCurrent) oss << " currentValue=" << measurement.currentValue;
        oss << " delta=" << measurement.delta;
        oss << " baselineAccepted=" << (measurement.baselineAccepted ? "true" : "false");
        oss << " currentAccepted=" << (measurement.currentAccepted ? "true" : "false");
      }
    }
    return oss.str();
  }

}  // namespace zs
