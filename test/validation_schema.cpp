#include <cassert>

#include "zensim/execution/ValidationSchema.hpp"

int main() {
  using namespace zs;

  {
    ValidationThreshold threshold{};
    assert(threshold.satisfied_by(123.0));

    threshold.mode = ValidationThresholdMode::less_equal;
    threshold.reference = 10.0;
    threshold.tolerance = 1.0;
    assert(threshold.satisfied_by(10.5));
    assert(threshold.satisfied_by(11.0));
    assert(!threshold.satisfied_by(11.01));

    threshold.mode = ValidationThresholdMode::greater_equal;
    threshold.reference = 20.0;
    threshold.tolerance = 0.5;
    assert(threshold.satisfied_by(19.5));
    assert(threshold.satisfied_by(20.0));
    assert(!threshold.satisfied_by(19.49));

    threshold.mode = ValidationThresholdMode::inclusive_range;
    threshold.lowerBound = 3.0;
    threshold.upperBound = 6.0;
    assert(threshold.satisfied_by(3.0));
    assert(threshold.satisfied_by(4.5));
    assert(threshold.satisfied_by(6.0));
    assert(!threshold.satisfied_by(2.99));
    assert(!threshold.satisfied_by(6.01));
  }

  {
    ValidationRecord passRecord{};
    passRecord.recordId = "async.runtime.latency";
    passRecord.suite = "async";
    passRecord.name = "runtime-pass";
    passRecord.backend = "thread_pool";
    passRecord.executor = "thread_pool";
    passRecord.target = "host";
    passRecord.set_metadata("audio.sampleRate", "48000");
    passRecord.set_metadata("audio.bufferSize", "256");
    passRecord.kind = ValidationRecordKind::benchmark;
    passRecord.outcome = ValidationOutcome::pass;
    passRecord.durationNs = 1200;
    passRecord.measurements.push_back(
        ValidationMeasurement{"latency", "ms", 8.5,
                              ValidationThreshold{ValidationThresholdMode::less_equal, 9.0, 0.0}});
    passRecord.measurements.push_back(
        ValidationMeasurement{"throughput", "ops", 102.0,
                              ValidationThreshold{ValidationThresholdMode::greater_equal, 100.0,
                                                  0.0}});
    assert(passRecord.accepted());

    ValidationRecord failRecord = passRecord;
    failRecord.name = "runtime-fail";
    failRecord.outcome = ValidationOutcome::fail;
    failRecord.measurements[0].value = 12.0;
    assert(!failRecord.accepted());

    ValidationRecord skipRecord{};
    skipRecord.suite = "async";
    skipRecord.name = "runtime-skip";
    skipRecord.outcome = ValidationOutcome::skip;
    assert(skipRecord.accepted());

    ValidationRecord errorRecord{};
    errorRecord.suite = "async";
    errorRecord.name = "runtime-error";
    errorRecord.outcome = ValidationOutcome::error;
    assert(!errorRecord.accepted());

    ValidationSuiteReport report{};
    report.suite = "async";
    report.set_metadata("profile", "runtime");
    report.records.push_back(passRecord);
    report.records.push_back(failRecord);
    report.records.push_back(skipRecord);
    report.records.push_back(errorRecord);
    report.refresh_summary();

    assert(report.schemaVersion == "zpc.validation.v1");
   assert(report.records[0].has_stable_id());
   assert(report.records[0].recordId == "async.runtime.latency");
    assert(report.records[0].has_metadata("audio.sampleRate"));
    assert(report.records[0].metadata_value("audio.sampleRate") == "48000");
    assert(report.metadata_value("profile") == "runtime");
    assert(report.summary.total == 4);
    assert(report.summary.passed == 1);
    assert(report.summary.failed == 1);
    assert(report.summary.skipped == 1);
    assert(report.summary.errored == 1);
  }

  return 0;
}
