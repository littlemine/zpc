#include <cassert>

#include "zensim/execution/ValidationCompare.hpp"

int main() {
  using namespace zs;

  ValidationRecord baselinePass{};
  baselinePass.recordId = "async.cuda.queue-latency";
  baselinePass.suite = "async";
  baselinePass.name = "queue-latency";
  baselinePass.backend = "cuda";
  baselinePass.executor = "cuda_record";
  baselinePass.target = "gpu0";
  baselinePass.kind = ValidationRecordKind::benchmark;
  baselinePass.outcome = ValidationOutcome::pass;
  baselinePass.measurements.push_back(ValidationMeasurement{
      "latency", "ms", 1.0,
      ValidationThreshold{ValidationThresholdMode::less_equal, 2.0, 0.0}});

  ValidationRecord currentRegressed = baselinePass;
  currentRegressed.outcome = ValidationOutcome::fail;
  currentRegressed.measurements[0].value = 3.0;

  ValidationRecord currentAdded{};
  currentAdded.suite = "async";
  currentAdded.name = "bandwidth";
  currentAdded.backend = "cuda";
  currentAdded.executor = "cuda_record";
  currentAdded.target = "gpu0";
  currentAdded.kind = ValidationRecordKind::benchmark;
  currentAdded.outcome = ValidationOutcome::pass;
  currentAdded.measurements.push_back(ValidationMeasurement{
      "throughput", "gbps", 500.0,
      ValidationThreshold{ValidationThresholdMode::greater_equal, 400.0, 0.0}});

  ValidationSuiteReport baseline{};
  baseline.suite = "async";
  baseline.records.push_back(baselinePass);

  ValidationSuiteReport current{};
  current.suite = "async";
  current.records.push_back(currentRegressed);
  current.records.push_back(currentAdded);

  const auto report = compare_validation_reports(baseline, current);
  assert(report.suite == "async");
  assert(!report.accepted);
  assert(report.summary.total == 2);
  assert(report.summary.regressed == 1);
  assert(report.summary.added == 1);
  assert(report.summary.unchanged == 0);
  assert(report.summary.improved == 0);
  assert(report.summary.removed == 0);

  assert(report.records.size() == 2);
  assert(report.records[0].recordId == "async.cuda.queue-latency");
  assert(report.records[0].name == "queue-latency");
  assert(report.records[0].status == ValidationDiffStatus::regressed);
  assert(report.records[0].measurements.size() == 1);
  assert(report.records[0].measurements[0].name == "latency");
  assert(report.records[0].measurements[0].status == ValidationDiffStatus::regressed);
  assert(report.records[0].measurements[0].baselineAccepted);
  assert(!report.records[0].measurements[0].currentAccepted);
  assert(report.records[0].measurements[0].delta == 2.0);

  assert(report.records[1].name == "bandwidth");
  assert(report.records[1].status == ValidationDiffStatus::added);
  assert(report.records[1].measurements.size() == 1);
  assert(report.records[1].measurements[0].status == ValidationDiffStatus::added);
  assert(!report.records[1].measurements[0].hasBaseline);
  assert(report.records[1].measurements[0].hasCurrent);

  ValidationRecord improvedBaseline = baselinePass;
  improvedBaseline.outcome = ValidationOutcome::fail;
  improvedBaseline.measurements[0].value = 3.5;
  ValidationSuiteReport baseline2{};
  baseline2.suite = "async";
  baseline2.records.push_back(improvedBaseline);

  ValidationSuiteReport current2{};
  current2.suite = "async";
  current2.records.push_back(baselinePass);

  const auto improved = compare_validation_reports(baseline2, current2);
  assert(improved.accepted);
  assert(improved.summary.total == 1);
  assert(improved.summary.improved == 1);
  assert(improved.records[0].status == ValidationDiffStatus::improved);
  assert(improved.records[0].measurements[0].status == ValidationDiffStatus::improved);

  ValidationRecord fallbackBaseline{};
  fallbackBaseline.suite = "async";
  fallbackBaseline.name = "tuple-match";
  fallbackBaseline.backend = "host";
  fallbackBaseline.executor = "inline";
  fallbackBaseline.target = "cpu";
  fallbackBaseline.kind = ValidationRecordKind::validation;
  fallbackBaseline.outcome = ValidationOutcome::pass;

  ValidationRecord fallbackCurrent = fallbackBaseline;
  fallbackCurrent.outcome = ValidationOutcome::fail;

  ValidationSuiteReport baseline3{};
  baseline3.suite = "async";
  baseline3.records.push_back(fallbackBaseline);

  ValidationSuiteReport current3{};
  current3.suite = "async";
  current3.records.push_back(fallbackCurrent);

  const auto fallbackReport = compare_validation_reports(baseline3, current3);
  assert(fallbackReport.records.size() == 1);
  assert(fallbackReport.records[0].recordId.size() == 0);
  assert(fallbackReport.records[0].status == ValidationDiffStatus::regressed);

  ValidationRecord idBaseline = baselinePass;
  idBaseline.name = "legacy-name";
  ValidationRecord idCurrent = baselinePass;
  idCurrent.name = "renamed-name";
  ValidationSuiteReport baseline4{};
  baseline4.suite = "async";
  baseline4.records.push_back(idBaseline);
  ValidationSuiteReport current4{};
  current4.suite = "async";
  current4.records.push_back(idCurrent);
  const auto idPreferred = compare_validation_reports(baseline4, current4);
  assert(idPreferred.records.size() == 1);
  assert(idPreferred.records[0].status == ValidationDiffStatus::unchanged);
  assert(idPreferred.records[0].name == "renamed-name");

  return 0;
}