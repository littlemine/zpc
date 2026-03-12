#include <filesystem>
#include <iostream>
#include <string>

#include "zensim/execution/ValidationCompare.hpp"
#include "zensim/execution/ValidationPersistence.hpp"

int main() {
  using namespace zs;

  const auto require = [](bool condition, const char *message) {
    if (!condition) {
      std::cerr << message << "\n";
      return false;
    }
    return true;
  };

  ValidationRecord baselineRecord{};
  baselineRecord.recordId = "async.cuda.persistence";
  baselineRecord.suite = "async";
  baselineRecord.name = "persistence";
  baselineRecord.backend = "cuda";
  baselineRecord.executor = "cuda_record";
  baselineRecord.target = "gpu0";
  baselineRecord.set_metadata("audio.sampleRate", "48000");
  baselineRecord.kind = ValidationRecordKind::benchmark;
  baselineRecord.outcome = ValidationOutcome::pass;
  baselineRecord.durationNs = 55;
  baselineRecord.measurements.push_back(ValidationMeasurement{
      "latency", "ms", 1.5,
      ValidationThreshold{ValidationThresholdMode::less_equal, 2.0, 0.0}});

  ValidationSuiteReport baseline{};
  baseline.suite = "async";
  baseline.set_metadata("profile", "runtime");
  baseline.records.push_back(baselineRecord);

  std::string errorMessage;
  ValidationSuiteReport parsed{};
  if (!require(parse_validation_report_json(format_validation_report_json(baseline), &parsed,
                                            &errorMessage),
               "round-trip JSON parse failed"))
    return 1;
  if (!require(errorMessage.empty(), "round-trip parse produced an unexpected error")) return 1;
  if (!require(std::string(parsed.suite.asChars()) == "async", "parsed suite mismatch"))
    return 1;
  if (!require(parsed.summary.total == 1, "parsed summary total mismatch")) return 1;
  if (!require(parsed.summary.passed == 1, "parsed summary passed mismatch")) return 1;
  if (!require(parsed.records.size() == 1, "parsed record count mismatch")) return 1;
  if (!require(std::string(parsed.records[0].recordId.asChars()) == "async.cuda.persistence",
               "parsed record id mismatch"))
    return 1;
  if (!require(parsed.records[0].measurements.size() == 1,
               "parsed measurement count mismatch"))
    return 1;
  if (!require(parsed.records[0].measurements[0].value == 1.5,
               "parsed measurement value mismatch"))
    return 1;
  if (!require(std::string(parsed.metadata_value("profile").asChars()) == "runtime",
               "parsed report metadata mismatch"))
    return 1;
  if (!require(std::string(parsed.records[0].metadata_value("audio.sampleRate").asChars())
                   == "48000",
               "parsed record metadata mismatch"))
    return 1;

  ValidationSuiteReport invalidTarget{};
  if (!require(!parse_validation_report_json("{", &invalidTarget, &errorMessage),
               "invalid JSON unexpectedly parsed"))
    return 1;
  if (!require(errorMessage.find("parse") != std::string::npos,
               "invalid JSON parse error message mismatch"))
    return 1;
  if (!require(!parse_validation_report_json("{}", &invalidTarget, &errorMessage),
               "missing suite JSON unexpectedly parsed"))
    return 1;
  if (!require(errorMessage.find("suite") != std::string::npos,
               "missing suite error message mismatch"))
    return 1;
  if (!require(!parse_validation_report_json(
             "{\"suite\":\"async\",\"records\":[{\"name\":\"broken\",\"kind\":\"validation\",\"outcome\":\"pass\",\"measurements\":[{\"name\":\"latency\",\"unit\":\"ms\",\"value\":1.0}]}]}",
                   &invalidTarget, &errorMessage),
               "missing threshold JSON unexpectedly parsed"))
    return 1;
  if (!require(errorMessage.find("threshold") != std::string::npos,
               "missing threshold error message mismatch"))
    return 1;

  const auto baselinePath = std::filesystem::temp_directory_path()
                          / "zpc_validation_persistence_baseline.json";
  const auto missingPath = std::filesystem::temp_directory_path()
                         / "zpc_validation_persistence_missing.json";
  std::error_code removeError;
  std::filesystem::remove(baselinePath, removeError);
  std::filesystem::remove(missingPath, removeError);

  if (!require(save_validation_report_json_file(baseline, baselinePath.string(), &errorMessage),
               "baseline file save failed"))
    return 1;
  if (!require(errorMessage.empty(), "baseline file save produced an unexpected error"))
    return 1;
  if (!require(std::filesystem::exists(baselinePath), "baseline file was not created"))
    return 1;

  ValidationSuiteReport loaded{};
  if (!require(load_validation_report_json_file(baselinePath.string(), &loaded, &errorMessage),
               "baseline file load failed"))
    return 1;
  if (!require(errorMessage.empty(), "baseline file load produced an unexpected error"))
    return 1;
  if (!require(loaded.summary.total == 1, "loaded summary total mismatch")) return 1;
  if (!require(loaded.records[0].accepted(), "loaded record acceptance mismatch")) return 1;

  ValidationSuiteReport current = loaded;
  current.records[0].outcome = ValidationOutcome::fail;
  current.records[0].measurements[0].value = 3.0;

  ValidationComparisonReport comparison{};
  ValidationSuiteReport loadedBaseline{};
  if (!require(compare_validation_report_to_baseline_file(baselinePath.string(), current,
                                                          &comparison, &loadedBaseline,
                                                          &errorMessage),
               "baseline comparison failed"))
    return 1;
  if (!require(errorMessage.empty(), "baseline comparison produced an unexpected error"))
    return 1;
  if (!require(std::string(loadedBaseline.records[0].name.asChars()) == "persistence",
               "loaded baseline record name mismatch"))
    return 1;
  if (!require(comparison.summary.total == 1, "comparison total mismatch")) return 1;
  if (!require(comparison.summary.regressed == 1, "comparison regressed mismatch")) return 1;
  if (!require(!comparison.accepted, "comparison acceptance mismatch")) return 1;

  if (!require(!load_validation_report_json_file(missingPath.string(), &loaded, &errorMessage),
               "missing baseline file unexpectedly loaded"))
    return 1;
  if (!require(errorMessage.find("open validation file") != std::string::npos,
               "missing baseline load error message mismatch"))
    return 1;
  if (!require(!compare_validation_report_to_baseline_file(missingPath.string(), current,
                                                           &comparison, nullptr,
                                                           &errorMessage),
               "missing baseline comparison unexpectedly succeeded"))
    return 1;
  if (!require(errorMessage.find("open validation file") != std::string::npos,
               "missing baseline compare error message mismatch"))
    return 1;

  std::filesystem::remove(baselinePath, removeError);
  return 0;
}
