#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayCombat.hpp"
#include "zensim/gameplay/GameplayEffect.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayProgression.hpp"
#include "zensim/execution/ValidationSchema.hpp"

namespace zs {

  // =====================================================================
  //  Metric Sample — a single recorded data point
  // =====================================================================

  /// A single metric observation with optional entity/category context.
  struct MetricSample {
    SmallString metricName{};     ///< e.g. "damage_dealt", "heal_amount"
    SmallString category{};      ///< e.g. "ability:fireball", "effect:regen"
    u64 entityId{0};             ///< Originating entity (0 = global)
    double value{0.0};           ///< Measured value
    u64 tick{0};                 ///< Simulation tick when sampled
  };

  // =====================================================================
  //  Statistical Summary
  // =====================================================================

  /// Aggregated statistics for a metric series.
  struct StatisticalSummary {
    SmallString metricName{};
    size_t count{0};
    double sum{0.0};
    double min{0.0};
    double max{0.0};
    double mean{0.0};
    double variance{0.0};
    double stddev{0.0};
    double median{0.0};

    /// Compute summary from a vector of values.
    static StatisticalSummary compute(const SmallString &name,
                                       std::vector<double> values) {
      StatisticalSummary s;
      s.metricName = name;
      s.count = values.size();
      if (s.count == 0) return s;

      std::sort(values.begin(), values.end());

      s.min = values.front();
      s.max = values.back();
      s.sum = 0.0;
      for (double v : values) s.sum += v;
      s.mean = s.sum / static_cast<double>(s.count);

      // Variance (population)
      double varSum = 0.0;
      for (double v : values) {
        double diff = v - s.mean;
        varSum += diff * diff;
      }
      s.variance = varSum / static_cast<double>(s.count);
      s.stddev = std::sqrt(s.variance);

      // Median
      if (s.count % 2 == 0) {
        s.median = (values[s.count / 2 - 1] + values[s.count / 2]) / 2.0;
      } else {
        s.median = values[s.count / 2];
      }

      return s;
    }
  };

  // =====================================================================
  //  Metric Collector — accumulates metric samples
  // =====================================================================

  /// Collects raw metric samples during gameplay simulation.
  /// Thread-unsafe by design (single-threaded gameplay tick loop).
  class MetricCollector {
  public:
    /// Record a raw metric sample.
    void record(const MetricSample &sample) {
      samples_.push_back(sample);
    }

    /// Convenience: record a named numeric value.
    void record(const SmallString &name, double value, u64 tick = 0,
                u64 entityId = 0, const SmallString &category = SmallString{}) {
      MetricSample s;
      s.metricName = name;
      s.value = value;
      s.tick = tick;
      s.entityId = entityId;
      s.category = category;
      samples_.push_back(s);
    }

    /// Get all samples.
    const std::vector<MetricSample> &samples() const noexcept {
      return samples_;
    }

    /// Get samples filtered by metric name.
    std::vector<MetricSample> samples_by_name(const SmallString &name) const {
      std::vector<MetricSample> result;
      for (auto &s : samples_) {
        if (std::strcmp(s.metricName.asChars(), name.asChars()) == 0) {
          result.push_back(s);
        }
      }
      return result;
    }

    /// Get samples filtered by entity.
    std::vector<MetricSample> samples_by_entity(u64 entityId) const {
      std::vector<MetricSample> result;
      for (auto &s : samples_) {
        if (s.entityId == entityId) {
          result.push_back(s);
        }
      }
      return result;
    }

    /// Extract values for a given metric name (for statistical analysis).
    std::vector<double> values_for(const SmallString &name) const {
      std::vector<double> vals;
      for (auto &s : samples_) {
        if (std::strcmp(s.metricName.asChars(), name.asChars()) == 0) {
          vals.push_back(s.value);
        }
      }
      return vals;
    }

    /// Compute statistical summary for a named metric.
    StatisticalSummary summarize(const SmallString &name) const {
      return StatisticalSummary::compute(name, values_for(name));
    }

    /// Get all unique metric names recorded.
    std::vector<SmallString> metric_names() const {
      std::vector<SmallString> names;
      for (auto &s : samples_) {
        bool found = false;
        for (auto &n : names) {
          if (std::strcmp(n.asChars(), s.metricName.asChars()) == 0) {
            found = true;
            break;
          }
        }
        if (!found) names.push_back(s.metricName);
      }
      return names;
    }

    /// Total number of samples recorded.
    size_t sample_count() const noexcept { return samples_.size(); }

    /// Clear all recorded samples.
    void clear() { samples_.clear(); }

  private:
    std::vector<MetricSample> samples_{};
  };

  // =====================================================================
  //  Combat Telemetry Hook
  // =====================================================================

  /// Listens to combat events and records telemetry data.
  /// Designed to plug into GameplayEventDispatcher via subscribe.
  class CombatTelemetryHook {
  public:
    explicit CombatTelemetryHook(MetricCollector *collector) : collector_(collector) {}

    /// Record a combat result as telemetry samples.
    void record_combat_result(const CombatResult &result, u64 tick = 0) {
      if (!collector_) return;

      // Damage dealt
      collector_->record(SmallString{"damage_dealt"}, result.finalDamage, tick,
                         result.attackerId.value,
                         SmallString{"combat"});

      // Track hit/miss/crit
      if (!result.isMiss) {
        collector_->record(SmallString{"combat_hit"}, 1.0, tick,
                            result.attackerId.value);
      } else {
        collector_->record(SmallString{"combat_miss"}, 1.0, tick,
                            result.attackerId.value);
      }

      if (result.isCritical) {
        collector_->record(SmallString{"combat_crit"}, 1.0, tick,
                            result.attackerId.value);
      }

      // Damage mitigated
      double mitigated = result.rawDamage - result.finalDamage;
      if (mitigated > 0.0) {
        collector_->record(SmallString{"damage_mitigated"}, mitigated, tick,
                           result.defenderId.value);
      }
    }

    /// Record a healing event.
    void record_healing(u64 entityId, double amount, u64 tick = 0) {
      if (!collector_) return;
      collector_->record(SmallString{"heal_amount"}, amount, tick, entityId,
                         SmallString{"healing"});
    }

    /// Record a kill event.
    void record_kill(u64 killerId, u64 victimId, u64 tick = 0) {
      if (!collector_) return;
      collector_->record(SmallString{"kill"}, 1.0, tick, killerId);
      collector_->record(SmallString{"death"}, 1.0, tick, victimId);
    }

    /// Record an ability usage.
    void record_ability_use(u64 entityId, u64 abilityDescId, u64 tick = 0) {
      if (!collector_) return;
      MetricSample s;
      s.metricName = SmallString{"ability_use"};
      s.entityId = entityId;
      s.value = static_cast<double>(abilityDescId);
      s.tick = tick;
      s.category = SmallString{"ability"};
      collector_->record(s);
    }

    MetricCollector *collector() const noexcept { return collector_; }

  private:
    MetricCollector *collector_{nullptr};
  };

  // =====================================================================
  //  Balance Threshold — defines acceptable ranges for metrics
  // =====================================================================

  /// A named balance threshold for regression detection.
  struct BalanceThreshold {
    SmallString metricName{};
    double expectedMin{0.0};
    double expectedMax{0.0};
    SmallString description{};
  };

  // =====================================================================
  //  Balance Analyzer — aggregate metrics and check thresholds
  // =====================================================================

  /// Analyzes collected metrics against balance thresholds
  /// and produces validation reports.
  class BalanceAnalyzer {
  public:
    /// Register a balance threshold.
    void register_threshold(const BalanceThreshold &threshold) {
      thresholds_.push_back(threshold);
    }

    /// Register multiple thresholds.
    void register_thresholds(const std::vector<BalanceThreshold> &thresholds) {
      for (auto &t : thresholds) thresholds_.push_back(t);
    }

    size_t threshold_count() const noexcept { return thresholds_.size(); }

    /// Analyze a collector's data against registered thresholds.
    /// Returns a ValidationSuiteReport for integration with ZPC validation.
    ValidationSuiteReport analyze(const MetricCollector &collector,
                                   const SmallString &suiteName = SmallString{"gameplay_balance"}) const {
      ValidationSuiteReport report;
      report.suite = suiteName;

      for (auto &threshold : thresholds_) {
        auto summary = collector.summarize(threshold.metricName);

        ValidationRecord record;
        record.suite = suiteName;
        record.name = threshold.metricName;
        record.kind = ValidationRecordKind::validation;
        record.note = threshold.description;

        // Add mean measurement with inclusive range threshold
        ValidationMeasurement meanMeasure;
        meanMeasure.name = SmallString{"mean"};
        meanMeasure.value = summary.mean;
        meanMeasure.threshold.mode = ValidationThresholdMode::inclusive_range;
        meanMeasure.threshold.lowerBound = threshold.expectedMin;
        meanMeasure.threshold.upperBound = threshold.expectedMax;
        record.measurements.push_back(meanMeasure);

        // Determine pass/fail
        if (summary.count == 0) {
          record.outcome = ValidationOutcome::skip;
          record.note = SmallString{"no_data"};
        } else if (meanMeasure.accepted()) {
          record.outcome = ValidationOutcome::pass;
        } else {
          record.outcome = ValidationOutcome::fail;
        }

        report.records.push_back(record);
      }

      report.refresh_summary();
      return report;
    }

    /// Analyze and return per-metric summaries (without threshold checks).
    std::vector<StatisticalSummary> summarize_all(const MetricCollector &collector) const {
      std::vector<StatisticalSummary> results;
      auto names = collector.metric_names();
      for (auto &name : names) {
        results.push_back(collector.summarize(name));
      }
      return results;
    }

    void clear_thresholds() { thresholds_.clear(); }

  private:
    std::vector<BalanceThreshold> thresholds_{};
  };

  // =====================================================================
  //  Telemetry Report Export — JSON and text formatting
  // =====================================================================

  /// Format a statistical summary as a human-readable string.
  inline std::string format_summary(const StatisticalSummary &s) {
    std::ostringstream os;
    os << s.metricName.asChars() << ": "
       << "n=" << s.count
       << " sum=" << s.sum
       << " min=" << s.min
       << " max=" << s.max
       << " mean=" << s.mean
       << " stddev=" << s.stddev
       << " median=" << s.median;
    return os.str();
  }

  /// Format a statistical summary as JSON.
  inline std::string summary_to_json(const StatisticalSummary &s) {
    std::ostringstream os;
    os << "{\"metric\":\"" << s.metricName.asChars()
       << "\",\"count\":" << s.count
       << ",\"sum\":" << s.sum
       << ",\"min\":" << s.min
       << ",\"max\":" << s.max
       << ",\"mean\":" << s.mean
       << ",\"stddev\":" << s.stddev
       << ",\"median\":" << s.median
       << ",\"variance\":" << s.variance
       << "}";
    return os.str();
  }

  /// Format multiple summaries as a JSON array.
  inline std::string summaries_to_json(const std::vector<StatisticalSummary> &summaries) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < summaries.size(); ++i) {
      if (i > 0) os << ",";
      os << summary_to_json(summaries[i]);
    }
    os << "]";
    return os.str();
  }

  /// Format a balance analysis report as JSON.
  inline std::string balance_report_to_json(const ValidationSuiteReport &report) {
    std::ostringstream os;
    os << "{\"suite\":\"" << report.suite.asChars()
       << "\",\"summary\":{\"total\":" << report.summary.total
       << ",\"passed\":" << report.summary.passed
       << ",\"failed\":" << report.summary.failed
       << ",\"skipped\":" << report.summary.skipped
       << "},\"records\":[";
    for (size_t i = 0; i < report.records.size(); ++i) {
      if (i > 0) os << ",";
      auto &r = report.records[i];
      const char *outcomeStr = "unknown";
      switch (r.outcome) {
        case ValidationOutcome::pass: outcomeStr = "pass"; break;
        case ValidationOutcome::fail: outcomeStr = "fail"; break;
        case ValidationOutcome::skip: outcomeStr = "skip"; break;
        case ValidationOutcome::error: outcomeStr = "error"; break;
      }
      os << "{\"name\":\"" << r.name.asChars()
         << "\",\"outcome\":\"" << outcomeStr
         << "\",\"note\":\"" << r.note.asChars()
         << "\",\"measurements\":[";
      for (size_t j = 0; j < r.measurements.size(); ++j) {
        if (j > 0) os << ",";
        auto &m = r.measurements[j];
        os << "{\"name\":\"" << m.name.asChars()
           << "\",\"value\":" << m.value
           << ",\"lower\":" << m.threshold.lowerBound
           << ",\"upper\":" << m.threshold.upperBound
           << ",\"accepted\":" << (m.accepted() ? "true" : "false")
           << "}";
      }
      os << "]}";
    }
    os << "]}";
    return os.str();
  }

  // =====================================================================
  //  Progression Telemetry Helper
  // =====================================================================

  /// Records progression-related metrics.
  class ProgressionTelemetryHook {
  public:
    explicit ProgressionTelemetryHook(MetricCollector *collector) : collector_(collector) {}

    /// Record XP award.
    void record_xp_award(u64 entityId, u64 amount, u64 tick = 0) {
      if (!collector_) return;
      collector_->record(SmallString{"xp_awarded"}, static_cast<double>(amount),
                         tick, entityId, SmallString{"progression"});
    }

    /// Record level-up.
    void record_level_up(u64 entityId, u32 newLevel, u64 tick = 0) {
      if (!collector_) return;
      collector_->record(SmallString{"level_up"}, static_cast<double>(newLevel),
                         tick, entityId, SmallString{"progression"});
    }

    /// Record skill unlock.
    void record_skill_unlock(u64 entityId, u64 nodeId, u64 tick = 0) {
      if (!collector_) return;
      collector_->record(SmallString{"skill_unlock"}, static_cast<double>(nodeId),
                         tick, entityId, SmallString{"progression"});
    }

    MetricCollector *collector() const noexcept { return collector_; }

  private:
    MetricCollector *collector_{nullptr};
  };

  // =====================================================================
  //  Simulation Summary Builder — end-of-simulation summary
  // =====================================================================

  /// Builds a comprehensive simulation summary from collected metrics.
  class SimulationSummaryBuilder {
  public:
    void set_collector(const MetricCollector *collector) noexcept {
      collector_ = collector;
    }

    /// Build a full simulation summary report.
    /// Returns a ValidationSuiteReport with all metrics as benchmark records.
    ValidationSuiteReport build(const SmallString &suiteName = SmallString{"simulation_summary"}) const {
      ValidationSuiteReport report;
      report.suite = suiteName;

      if (!collector_) {
        report.refresh_summary();
        return report;
      }

      auto names = collector_->metric_names();
      for (auto &name : names) {
        auto summary = collector_->summarize(name);

        ValidationRecord record;
        record.suite = suiteName;
        record.name = name;
        record.kind = ValidationRecordKind::benchmark;
        record.outcome = ValidationOutcome::pass;

        // Add mean measurement (no threshold — just recording)
        ValidationMeasurement meanMeasure;
        meanMeasure.name = SmallString{"mean"};
        meanMeasure.value = summary.mean;
        record.measurements.push_back(meanMeasure);

        // Add count measurement
        ValidationMeasurement countMeasure;
        countMeasure.name = SmallString{"count"};
        countMeasure.value = static_cast<double>(summary.count);
        record.measurements.push_back(countMeasure);

        // Add sum measurement
        ValidationMeasurement sumMeasure;
        sumMeasure.name = SmallString{"sum"};
        sumMeasure.value = summary.sum;
        record.measurements.push_back(sumMeasure);

        report.records.push_back(record);
      }

      report.refresh_summary();
      return report;
    }

  private:
    const MetricCollector *collector_{nullptr};
  };

}  // namespace zs
