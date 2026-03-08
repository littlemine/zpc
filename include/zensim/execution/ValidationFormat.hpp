#pragma once

#include <sstream>
#include <string>

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
    }
    return oss.str();
  }

}  // namespace zs