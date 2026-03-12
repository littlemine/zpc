#pragma once

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "zensim/execution/ValidationCompare.hpp"
#include "zensim/execution/ValidationFormat.hpp"
#include "zensim/zpc_tpls/rapidjson/document.h"

namespace zs {

  namespace detail {

    inline void set_validation_persistence_error(std::string *errorMessage,
                                                 std::string_view message) {
      if (errorMessage) errorMessage->assign(message.data(), message.size());
    }

    inline bool parse_validation_threshold_mode(std::string_view text,
                                                ValidationThresholdMode *mode) {
      if (text == "none") {
        *mode = ValidationThresholdMode::none;
        return true;
      }
      if (text == "less_equal") {
        *mode = ValidationThresholdMode::less_equal;
        return true;
      }
      if (text == "greater_equal") {
        *mode = ValidationThresholdMode::greater_equal;
        return true;
      }
      if (text == "inclusive_range") {
        *mode = ValidationThresholdMode::inclusive_range;
        return true;
      }
      return false;
    }

    inline bool parse_validation_record_kind(std::string_view text, ValidationRecordKind *kind) {
      if (text == "validation") {
        *kind = ValidationRecordKind::validation;
        return true;
      }
      if (text == "benchmark") {
        *kind = ValidationRecordKind::benchmark;
        return true;
      }
      return false;
    }

    inline bool parse_validation_outcome(std::string_view text, ValidationOutcome *outcome) {
      if (text == "pass") {
        *outcome = ValidationOutcome::pass;
        return true;
      }
      if (text == "fail") {
        *outcome = ValidationOutcome::fail;
        return true;
      }
      if (text == "skip") {
        *outcome = ValidationOutcome::skip;
        return true;
      }
      if (text == "error") {
        *outcome = ValidationOutcome::error;
        return true;
      }
      return false;
    }

    inline bool parse_json_string_field(const rapidjson::Value &object, const char *name,
                                        SmallString *value, bool required,
                                        std::string *errorMessage) {
      const auto member = object.FindMember(name);
      if (member == object.MemberEnd()) {
        if (!required) return true;
        set_validation_persistence_error(errorMessage,
                                         std::string{"missing string field: "} + name);
        return false;
      }
      if (!member->value.IsString()) {
        set_validation_persistence_error(errorMessage,
                                         std::string{"invalid string field: "} + name);
        return false;
      }
      *value = SmallString{member->value.GetString()};
      return true;
    }

    inline bool parse_json_number_field(const rapidjson::Value &object, const char *name,
                                        double *value, bool required,
                                        std::string *errorMessage) {
      const auto member = object.FindMember(name);
      if (member == object.MemberEnd()) {
        if (!required) return true;
        set_validation_persistence_error(errorMessage,
                                         std::string{"missing numeric field: "} + name);
        return false;
      }
      if (!member->value.IsNumber()) {
        set_validation_persistence_error(errorMessage,
                                         std::string{"invalid numeric field: "} + name);
        return false;
      }
      *value = member->value.GetDouble();
      return true;
    }

    inline bool parse_validation_threshold(const rapidjson::Value &object,
                                           ValidationThreshold *threshold,
                                           std::string *errorMessage) {
      if (!object.IsObject()) {
        set_validation_persistence_error(errorMessage, "threshold must be an object");
        return false;
      }

      SmallString modeName{};
      if (!parse_json_string_field(object, "mode", &modeName, true, errorMessage)) return false;
      if (!parse_validation_threshold_mode(modeName.asChars(), &threshold->mode)) {
        set_validation_persistence_error(errorMessage,
                                         std::string{"unknown threshold mode: "}
                                             + modeName.asChars());
        return false;
      }

      if (!parse_json_number_field(object, "reference", &threshold->reference, false,
                                   errorMessage))
        return false;
      if (!parse_json_number_field(object, "tolerance", &threshold->tolerance, false,
                                   errorMessage))
        return false;
      if (!parse_json_number_field(object, "lowerBound", &threshold->lowerBound, false,
                                   errorMessage))
        return false;
      if (!parse_json_number_field(object, "upperBound", &threshold->upperBound, false,
                                   errorMessage))
        return false;

      return true;
    }

    inline bool parse_validation_measurement(const rapidjson::Value &object,
                                             ValidationMeasurement *measurement,
                                             std::string *errorMessage) {
      if (!object.IsObject()) {
        set_validation_persistence_error(errorMessage, "measurement must be an object");
        return false;
      }

      if (!parse_json_string_field(object, "name", &measurement->name, true, errorMessage))
        return false;
      if (!parse_json_string_field(object, "unit", &measurement->unit, false, errorMessage))
        return false;
      if (!parse_json_number_field(object, "value", &measurement->value, true, errorMessage))
        return false;

      const auto threshold = object.FindMember("threshold");
      if (threshold == object.MemberEnd()) {
        set_validation_persistence_error(errorMessage, "missing threshold field");
        return false;
      }

      return parse_validation_threshold(threshold->value, &measurement->threshold, errorMessage);
    }

    inline bool parse_validation_metadata_entries(const rapidjson::Value &array,
                                                  std::vector<ValidationMetadataEntry> *metadata,
                                                  std::string *errorMessage) {
      if (!metadata) {
        set_validation_persistence_error(errorMessage, "metadata output must not be null");
        return false;
      }
      if (!array.IsArray()) {
        set_validation_persistence_error(errorMessage, "metadata must be an array");
        return false;
      }

      metadata->clear();
      metadata->reserve(array.Size());
      for (const auto &entryValue : array.GetArray()) {
        if (!entryValue.IsObject()) {
          set_validation_persistence_error(errorMessage, "metadata entry must be an object");
          return false;
        }
        ValidationMetadataEntry entry{};
        if (!parse_json_string_field(entryValue, "key", &entry.key, true, errorMessage))
          return false;
        if (!parse_json_string_field(entryValue, "value", &entry.value, false, errorMessage))
          return false;
        metadata->push_back(entry);
      }
      return true;
    }

    inline bool parse_validation_record(const rapidjson::Value &object, ValidationRecord *record,
                                        std::string *errorMessage) {
      if (!object.IsObject()) {
        set_validation_persistence_error(errorMessage, "record must be an object");
        return false;
      }

      if (!parse_json_string_field(object, "recordId", &record->recordId, false, errorMessage))
        return false;
      if (!parse_json_string_field(object, "suite", &record->suite, false, errorMessage))
        return false;
      if (!parse_json_string_field(object, "name", &record->name, true, errorMessage))
        return false;
      if (!parse_json_string_field(object, "backend", &record->backend, false, errorMessage))
        return false;
      if (!parse_json_string_field(object, "executor", &record->executor, false,
                                   errorMessage))
        return false;
      if (!parse_json_string_field(object, "target", &record->target, false, errorMessage))
        return false;
      if (!parse_json_string_field(object, "note", &record->note, false, errorMessage))
        return false;

      const auto metadata = object.FindMember("metadata");
      if (metadata != object.MemberEnd()) {
        if (!parse_validation_metadata_entries(metadata->value, &record->metadata, errorMessage))
          return false;
      }

      SmallString kindName{};
      if (!parse_json_string_field(object, "kind", &kindName, true, errorMessage)) return false;
      if (!parse_validation_record_kind(kindName.asChars(), &record->kind)) {
        set_validation_persistence_error(errorMessage,
                                         std::string{"unknown record kind: "}
                                             + kindName.asChars());
        return false;
      }

      SmallString outcomeName{};
      if (!parse_json_string_field(object, "outcome", &outcomeName, true, errorMessage))
        return false;
      if (!parse_validation_outcome(outcomeName.asChars(), &record->outcome)) {
        set_validation_persistence_error(errorMessage,
                                         std::string{"unknown record outcome: "}
                                             + outcomeName.asChars());
        return false;
      }

      const auto duration = object.FindMember("durationNs");
      if (duration != object.MemberEnd()) {
        if (!duration->value.IsUint64()) {
          set_validation_persistence_error(errorMessage, "invalid durationNs field");
          return false;
        }
        record->durationNs = duration->value.GetUint64();
      }

      const auto measurements = object.FindMember("measurements");
      if (measurements == object.MemberEnd() || !measurements->value.IsArray()) {
        set_validation_persistence_error(errorMessage, "record measurements must be an array");
        return false;
      }

      record->measurements.clear();
      record->measurements.reserve(measurements->value.Size());
      for (const auto &measurementValue : measurements->value.GetArray()) {
        ValidationMeasurement measurement{};
        if (!parse_validation_measurement(measurementValue, &measurement, errorMessage))
          return false;
        record->measurements.push_back(measurement);
      }

      return true;
    }

  }  // namespace detail

  inline bool parse_validation_report_json(std::string_view json, ValidationSuiteReport *report,
                                           std::string *errorMessage = nullptr) {
    if (!report) {
      detail::set_validation_persistence_error(errorMessage, "report output must not be null");
      return false;
    }

    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError()) {
      detail::set_validation_persistence_error(errorMessage, "failed to parse validation JSON");
      return false;
    }
    if (!document.IsObject()) {
      detail::set_validation_persistence_error(errorMessage,
                                               "validation report root must be an object");
      return false;
    }

    ValidationSuiteReport parsed{};
    if (!detail::parse_json_string_field(document, "schemaVersion", &parsed.schemaVersion, false,
                                         errorMessage))
      return false;
    if (!detail::parse_json_string_field(document, "suite", &parsed.suite, true, errorMessage))
      return false;

    const auto metadata = document.FindMember("metadata");
    if (metadata != document.MemberEnd()) {
      if (!detail::parse_validation_metadata_entries(metadata->value, &parsed.metadata,
                                                     errorMessage))
        return false;
    }

    const auto records = document.FindMember("records");
    if (records == document.MemberEnd() || !records->value.IsArray()) {
      detail::set_validation_persistence_error(errorMessage, "report records must be an array");
      return false;
    }

    parsed.records.clear();
    parsed.records.reserve(records->value.Size());
    for (const auto &recordValue : records->value.GetArray()) {
      ValidationRecord record{};
      if (!detail::parse_validation_record(recordValue, &record, errorMessage)) return false;
      parsed.records.push_back(record);
    }

    parsed.refresh_summary();
    *report = parsed;
    if (errorMessage) errorMessage->clear();
    return true;
  }

  inline bool save_validation_report_json_file(const ValidationSuiteReport &report,
                                               const std::string &path,
                                               std::string *errorMessage = nullptr) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      detail::set_validation_persistence_error(errorMessage,
                                               std::string{"failed to open file for writing: "}
                                                   + path);
      return false;
    }

    output << format_validation_report_json(report);
    if (!output.good()) {
      detail::set_validation_persistence_error(errorMessage,
                                               std::string{"failed to write validation file: "}
                                                   + path);
      return false;
    }

    if (errorMessage) errorMessage->clear();
    return true;
  }

  inline bool load_validation_report_json_file(const std::string &path,
                                               ValidationSuiteReport *report,
                                               std::string *errorMessage = nullptr) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      detail::set_validation_persistence_error(errorMessage,
                                               std::string{"failed to open validation file: "}
                                                   + path);
      return false;
    }

    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
      detail::set_validation_persistence_error(errorMessage,
                                               std::string{"failed to read validation file: "}
                                                   + path);
      return false;
    }

    return parse_validation_report_json(json, report, errorMessage);
  }

  inline bool compare_validation_report_to_baseline_file(
      const std::string &path, const ValidationSuiteReport &current,
      ValidationComparisonReport *comparison, ValidationSuiteReport *baseline = nullptr,
      std::string *errorMessage = nullptr) {
    if (!comparison) {
      detail::set_validation_persistence_error(errorMessage,
                                               "comparison output must not be null");
      return false;
    }

    ValidationSuiteReport loadedBaseline{};
    if (!load_validation_report_json_file(path, &loadedBaseline, errorMessage)) return false;

    *comparison = compare_validation_reports(loadedBaseline, current);
    if (baseline) *baseline = loadedBaseline;
    if (errorMessage) errorMessage->clear();
    return true;
  }

}  // namespace zs
