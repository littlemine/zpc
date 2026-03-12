#include <cassert>
#include <string>

#include "zensim/execution/ValidationCompare.hpp"
#include "zensim/execution/ValidationFormat.hpp"

int main() {
  using namespace zs;

  ValidationRecord record{};
       record.recordId = "async.cuda.queue-sync";
  record.suite = "async";
  record.name = "queue\"sync";
  record.backend = "cuda";
  record.executor = "cuda_record";
  record.target = "gpu0";
  record.note = "line1\nline2";
  record.kind = ValidationRecordKind::benchmark;
  record.outcome = ValidationOutcome::pass;
  record.durationNs = 4200;
  record.measurements.push_back(ValidationMeasurement{
      "latency", "ms", 1.25,
      ValidationThreshold{ValidationThresholdMode::less_equal, 2.0, 0.0}});

  const std::string recordJson = format_validation_record_json(record);
       assert(recordJson.find("\"recordId\":\"async.cuda.queue-sync\"") != std::string::npos);
  assert(recordJson.find("\"suite\":\"async\"") != std::string::npos);
  assert(recordJson.find("\"name\":\"queue\\\"sync\"") != std::string::npos);
  assert(recordJson.find("\"note\":\"line1\\nline2\"") != std::string::npos);
  assert(recordJson.find("\"backend\":\"cuda\"") != std::string::npos);
  assert(recordJson.find("\"kind\":\"benchmark\"") != std::string::npos);
  assert(recordJson.find("\"outcome\":\"pass\"") != std::string::npos);
  assert(recordJson.find("\"accepted\":true") != std::string::npos);
  assert(recordJson.find("\"mode\":\"less_equal\"") != std::string::npos);

  ValidationRecord failed = record;
  failed.name = "queue-fail";
  failed.outcome = ValidationOutcome::fail;
  failed.measurements[0].value = 3.5;

  ValidationSuiteReport report{};
  report.suite = "async";
  report.records.push_back(record);
  report.records.push_back(failed);

  const std::string reportJson = format_validation_report_json(report);
  assert(reportJson.find("\"schemaVersion\":\"zpc.validation.v1\"") != std::string::npos);
  assert(reportJson.find("\"suite\":\"async\"") != std::string::npos);
  assert(reportJson.find("\"total\":2") != std::string::npos);
  assert(reportJson.find("\"passed\":1") != std::string::npos);
  assert(reportJson.find("\"failed\":1") != std::string::npos);

  const std::string summaryText = format_validation_summary_text(report);
  assert(summaryText.find("suite=async") != std::string::npos);
  assert(summaryText.find("schema=zpc.validation.v1") != std::string::npos);
  assert(summaryText.find("total=2") != std::string::npos);
  assert(summaryText.find("passed=1") != std::string::npos);
  assert(summaryText.find("failed=1") != std::string::npos);
  assert(summaryText.find("- [pass] queue\"sync recordId=async.cuda.queue-sync backend=cuda executor=cuda_record target=gpu0 kind=benchmark accepted=true durationNs=4200")
         != std::string::npos);
  assert(summaryText.find("- [fail] queue-fail recordId=async.cuda.queue-sync backend=cuda executor=cuda_record target=gpu0 kind=benchmark accepted=false durationNs=4200")
         != std::string::npos);

  const std::string reportText = format_validation_report_text(report);
  assert(reportText.find("note=line1\nline2") != std::string::npos);
  assert(reportText.find("* latency=1.25 ms accepted=true threshold.mode=less_equal threshold.reference=2 threshold.tolerance=0")
         != std::string::npos);

  const auto comparison = compare_validation_reports(report, report);
  const std::string comparisonSummary = format_validation_comparison_summary_text(comparison);
  assert(comparisonSummary.find("accepted=true") != std::string::npos);
  assert(comparisonSummary.find("unchanged=2") != std::string::npos);

  const std::string comparisonText = format_validation_comparison_report_text(comparison);
  assert(comparisonText.find("suite=async accepted=true total=2 unchanged=2") != std::string::npos);
  assert(comparisonText.find("* [unchanged] latency unit=ms baselineValue=1.25 currentValue=1.25 delta=0 baselineAccepted=true currentAccepted=true")
         != std::string::npos);

  return 0;
}
