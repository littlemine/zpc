/// @file gameplay_telemetry.cpp
/// @brief M11 unit tests — Telemetry, Metrics, Balance Analysis
///
/// Tests cover:
///   1.  MetricSample — default construction
///   2.  MetricSample — fields assignment
///   3.  MetricCollector — record single sample
///   4.  MetricCollector — record convenience overload
///   5.  MetricCollector — sample_count and clear
///   6.  MetricCollector — samples_by_name
///   7.  MetricCollector — samples_by_entity
///   8.  MetricCollector — values_for
///   9.  MetricCollector — metric_names unique
///  10.  MetricCollector — summarize
///  11.  StatisticalSummary — compute empty
///  12.  StatisticalSummary — compute single value
///  13.  StatisticalSummary — compute multiple values (mean/min/max/stddev/median)
///  14.  StatisticalSummary — compute even count median
///  15.  CombatTelemetryHook — null collector safety
///  16.  CombatTelemetryHook — record_combat_result hit
///  17.  CombatTelemetryHook — record_combat_result miss
///  18.  CombatTelemetryHook — record_combat_result crit
///  19.  CombatTelemetryHook — record_combat_result mitigated damage
///  20.  CombatTelemetryHook — record_healing
///  21.  CombatTelemetryHook — record_kill
///  22.  CombatTelemetryHook — record_ability_use
///  23.  ProgressionTelemetryHook — null collector safety
///  24.  ProgressionTelemetryHook — record_xp_award
///  25.  ProgressionTelemetryHook — record_level_up
///  26.  ProgressionTelemetryHook — record_skill_unlock
///  27.  BalanceAnalyzer — register_threshold
///  28.  BalanceAnalyzer — analyze pass
///  29.  BalanceAnalyzer — analyze fail
///  30.  BalanceAnalyzer — analyze skip (no data)
///  31.  BalanceAnalyzer — summarize_all
///  32.  SimulationSummaryBuilder — null collector
///  33.  SimulationSummaryBuilder — build with data
///  34.  format_summary — text output
///  35.  summary_to_json — JSON output
///  36.  summaries_to_json — JSON array
///  37.  balance_report_to_json — full report
///  38.  Integration: multi-round combat telemetry + balance check

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "zensim/gameplay/GameplayTelemetry.hpp"

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static const double kEps = 1e-6;

static bool near(double a, double b) {
  return std::fabs(a - b) < kEps;
}

/// Build a simple CombatResult for testing.
static zs::CombatResult make_hit(zs::u64 atk, zs::u64 def,
                                  double raw, double final_dmg,
                                  bool crit = false) {
  zs::CombatResult r;
  r.outcome = zs::CombatOutcome::hit;
  r.attackerId = zs::GameplayEntityId{atk};
  r.defenderId = zs::GameplayEntityId{def};
  r.rawDamage = raw;
  r.finalDamage = final_dmg;
  r.isMiss = false;
  r.isCritical = crit;
  return r;
}

static zs::CombatResult make_miss(zs::u64 atk, zs::u64 def) {
  zs::CombatResult r;
  r.outcome = zs::CombatOutcome::miss;
  r.attackerId = zs::GameplayEntityId{atk};
  r.defenderId = zs::GameplayEntityId{def};
  r.rawDamage = 0.0;
  r.finalDamage = 0.0;
  r.isMiss = true;
  r.isCritical = false;
  return r;
}

// ===========================================================================
int main() {
  int testNum = 0;

  // -----------------------------------------------------------------------
  //  1. MetricSample — default construction
  // -----------------------------------------------------------------------
  {
    zs::MetricSample s;
    assert(s.metricName.size() == 0);
    assert(s.category.size() == 0);
    assert(s.entityId == 0);
    assert(near(s.value, 0.0));
    assert(s.tick == 0);
    fprintf(stderr, "  [PASS] %d. MetricSample — default construction\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  2. MetricSample — fields assignment
  // -----------------------------------------------------------------------
  {
    zs::MetricSample s;
    s.metricName = zs::SmallString{"damage_dealt"};
    s.category = zs::SmallString{"combat"};
    s.entityId = 42;
    s.value = 100.5;
    s.tick = 7;
    assert(std::strcmp(s.metricName.asChars(), "damage_dealt") == 0);
    assert(std::strcmp(s.category.asChars(), "combat") == 0);
    assert(s.entityId == 42);
    assert(near(s.value, 100.5));
    assert(s.tick == 7);
    fprintf(stderr, "  [PASS] %d. MetricSample — fields assignment\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  3. MetricCollector — record single sample
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    assert(mc.sample_count() == 0);
    zs::MetricSample s;
    s.metricName = zs::SmallString{"test"};
    s.value = 10.0;
    mc.record(s);
    assert(mc.sample_count() == 1);
    assert(near(mc.samples()[0].value, 10.0));
    fprintf(stderr, "  [PASS] %d. MetricCollector — record single sample\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  4. MetricCollector — record convenience overload
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"hp_change"}, -25.0, 3, 99, zs::SmallString{"combat"});
    assert(mc.sample_count() == 1);
    auto &s = mc.samples()[0];
    assert(std::strcmp(s.metricName.asChars(), "hp_change") == 0);
    assert(near(s.value, -25.0));
    assert(s.tick == 3);
    assert(s.entityId == 99);
    assert(std::strcmp(s.category.asChars(), "combat") == 0);
    fprintf(stderr, "  [PASS] %d. MetricCollector — record convenience overload\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  5. MetricCollector — sample_count and clear
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"a"}, 1.0);
    mc.record(zs::SmallString{"b"}, 2.0);
    mc.record(zs::SmallString{"c"}, 3.0);
    assert(mc.sample_count() == 3);
    mc.clear();
    assert(mc.sample_count() == 0);
    assert(mc.samples().empty());
    fprintf(stderr, "  [PASS] %d. MetricCollector — sample_count and clear\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  6. MetricCollector — samples_by_name
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"damage"}, 10.0);
    mc.record(zs::SmallString{"healing"}, 5.0);
    mc.record(zs::SmallString{"damage"}, 20.0);
    mc.record(zs::SmallString{"healing"}, 8.0);
    mc.record(zs::SmallString{"damage"}, 15.0);

    auto dmg = mc.samples_by_name(zs::SmallString{"damage"});
    assert(dmg.size() == 3);
    assert(near(dmg[0].value, 10.0));
    assert(near(dmg[1].value, 20.0));
    assert(near(dmg[2].value, 15.0));

    auto heal = mc.samples_by_name(zs::SmallString{"healing"});
    assert(heal.size() == 2);

    auto none = mc.samples_by_name(zs::SmallString{"nonexist"});
    assert(none.empty());
    fprintf(stderr, "  [PASS] %d. MetricCollector — samples_by_name\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  7. MetricCollector — samples_by_entity
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"damage"}, 10.0, 0, 1);
    mc.record(zs::SmallString{"damage"}, 20.0, 0, 2);
    mc.record(zs::SmallString{"damage"}, 30.0, 0, 1);

    auto e1 = mc.samples_by_entity(1);
    assert(e1.size() == 2);
    assert(near(e1[0].value, 10.0));
    assert(near(e1[1].value, 30.0));

    auto e2 = mc.samples_by_entity(2);
    assert(e2.size() == 1);

    auto e3 = mc.samples_by_entity(999);
    assert(e3.empty());
    fprintf(stderr, "  [PASS] %d. MetricCollector — samples_by_entity\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  8. MetricCollector — values_for
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"dps"}, 100.0);
    mc.record(zs::SmallString{"hps"}, 50.0);
    mc.record(zs::SmallString{"dps"}, 120.0);
    mc.record(zs::SmallString{"dps"}, 80.0);

    auto vals = mc.values_for(zs::SmallString{"dps"});
    assert(vals.size() == 3);
    assert(near(vals[0], 100.0));
    assert(near(vals[1], 120.0));
    assert(near(vals[2], 80.0));
    fprintf(stderr, "  [PASS] %d. MetricCollector — values_for\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  9. MetricCollector — metric_names unique
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"alpha"}, 1.0);
    mc.record(zs::SmallString{"beta"}, 2.0);
    mc.record(zs::SmallString{"alpha"}, 3.0);
    mc.record(zs::SmallString{"gamma"}, 4.0);
    mc.record(zs::SmallString{"beta"}, 5.0);

    auto names = mc.metric_names();
    assert(names.size() == 3);
    // Order should be first-seen: alpha, beta, gamma
    assert(std::strcmp(names[0].asChars(), "alpha") == 0);
    assert(std::strcmp(names[1].asChars(), "beta") == 0);
    assert(std::strcmp(names[2].asChars(), "gamma") == 0);
    fprintf(stderr, "  [PASS] %d. MetricCollector — metric_names unique\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  10. MetricCollector — summarize
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"dmg"}, 10.0);
    mc.record(zs::SmallString{"dmg"}, 20.0);
    mc.record(zs::SmallString{"dmg"}, 30.0);

    auto summary = mc.summarize(zs::SmallString{"dmg"});
    assert(summary.count == 3);
    assert(near(summary.mean, 20.0));
    assert(near(summary.min, 10.0));
    assert(near(summary.max, 30.0));
    assert(near(summary.sum, 60.0));
    assert(near(summary.median, 20.0));
    fprintf(stderr, "  [PASS] %d. MetricCollector — summarize\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  11. StatisticalSummary — compute empty
  // -----------------------------------------------------------------------
  {
    auto s = zs::StatisticalSummary::compute(zs::SmallString{"empty"}, {});
    assert(s.count == 0);
    assert(near(s.sum, 0.0));
    assert(near(s.min, 0.0));
    assert(near(s.max, 0.0));
    assert(near(s.mean, 0.0));
    assert(near(s.stddev, 0.0));
    assert(near(s.median, 0.0));
    fprintf(stderr, "  [PASS] %d. StatisticalSummary — compute empty\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  12. StatisticalSummary — compute single value
  // -----------------------------------------------------------------------
  {
    auto s = zs::StatisticalSummary::compute(zs::SmallString{"one"}, {42.0});
    assert(s.count == 1);
    assert(near(s.sum, 42.0));
    assert(near(s.min, 42.0));
    assert(near(s.max, 42.0));
    assert(near(s.mean, 42.0));
    assert(near(s.stddev, 0.0));
    assert(near(s.median, 42.0));
    fprintf(stderr, "  [PASS] %d. StatisticalSummary — compute single value\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  13. StatisticalSummary — compute multiple values
  // -----------------------------------------------------------------------
  {
    // Values: 2, 4, 4, 4, 5, 5, 7, 9  (classic textbook)
    auto s = zs::StatisticalSummary::compute(zs::SmallString{"multi"},
               {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0});
    assert(s.count == 8);
    assert(near(s.sum, 40.0));
    assert(near(s.min, 2.0));
    assert(near(s.max, 9.0));
    assert(near(s.mean, 5.0));
    // Population variance = 4.0, stddev = 2.0
    assert(near(s.variance, 4.0));
    assert(near(s.stddev, 2.0));
    // Median of even count: (4+5)/2 = 4.5
    assert(near(s.median, 4.5));
    fprintf(stderr, "  [PASS] %d. StatisticalSummary — compute multiple values\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  14. StatisticalSummary — compute even count median
  // -----------------------------------------------------------------------
  {
    // 1, 3 → median = 2.0
    auto s = zs::StatisticalSummary::compute(zs::SmallString{"even"}, {3.0, 1.0});
    assert(s.count == 2);
    assert(near(s.median, 2.0));
    // Also check that sort happened (min=1, max=3)
    assert(near(s.min, 1.0));
    assert(near(s.max, 3.0));
    fprintf(stderr, "  [PASS] %d. StatisticalSummary — compute even count median\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  15. CombatTelemetryHook — null collector safety
  // -----------------------------------------------------------------------
  {
    zs::CombatTelemetryHook hook(nullptr);
    auto r = make_hit(1, 2, 50.0, 40.0);
    hook.record_combat_result(r);  // should not crash
    hook.record_healing(1, 10.0);
    hook.record_kill(1, 2);
    hook.record_ability_use(1, 100);
    assert(hook.collector() == nullptr);
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — null collector safety\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  16. CombatTelemetryHook — record_combat_result hit
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    auto r = make_hit(1, 2, 100.0, 80.0);
    hook.record_combat_result(r, 5);

    // Should record: damage_dealt, combat_hit, damage_mitigated
    assert(mc.sample_count() >= 3);

    auto dmg = mc.samples_by_name(zs::SmallString{"damage_dealt"});
    assert(dmg.size() == 1);
    assert(near(dmg[0].value, 80.0));
    assert(dmg[0].entityId == 1);
    assert(dmg[0].tick == 5);

    auto hits = mc.samples_by_name(zs::SmallString{"combat_hit"});
    assert(hits.size() == 1);

    auto misses = mc.samples_by_name(zs::SmallString{"combat_miss"});
    assert(misses.empty());

    auto mit = mc.samples_by_name(zs::SmallString{"damage_mitigated"});
    assert(mit.size() == 1);
    assert(near(mit[0].value, 20.0));  // 100 - 80
    assert(mit[0].entityId == 2);      // defender
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — record_combat_result hit\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  17. CombatTelemetryHook — record_combat_result miss
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    auto r = make_miss(1, 2);
    hook.record_combat_result(r, 10);

    auto dmg = mc.samples_by_name(zs::SmallString{"damage_dealt"});
    assert(dmg.size() == 1);
    assert(near(dmg[0].value, 0.0));

    auto misses = mc.samples_by_name(zs::SmallString{"combat_miss"});
    assert(misses.size() == 1);

    auto hits = mc.samples_by_name(zs::SmallString{"combat_hit"});
    assert(hits.empty());
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — record_combat_result miss\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  18. CombatTelemetryHook — record_combat_result crit
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    auto r = make_hit(1, 2, 200.0, 180.0, true);
    hook.record_combat_result(r, 15);

    auto crits = mc.samples_by_name(zs::SmallString{"combat_crit"});
    assert(crits.size() == 1);
    assert(crits[0].entityId == 1);

    auto hits = mc.samples_by_name(zs::SmallString{"combat_hit"});
    assert(hits.size() == 1);  // crit is still a hit
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — record_combat_result crit\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  19. CombatTelemetryHook — record_combat_result mitigated damage
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    // No mitigation case (raw == final)
    auto r = make_hit(1, 2, 50.0, 50.0);
    hook.record_combat_result(r);

    auto mit = mc.samples_by_name(zs::SmallString{"damage_mitigated"});
    assert(mit.empty());  // no mitigation recorded when mitigated <= 0
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — mitigated damage\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  20. CombatTelemetryHook — record_healing
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    hook.record_healing(5, 75.0, 20);

    assert(mc.sample_count() == 1);
    auto heal = mc.samples_by_name(zs::SmallString{"heal_amount"});
    assert(heal.size() == 1);
    assert(near(heal[0].value, 75.0));
    assert(heal[0].entityId == 5);
    assert(heal[0].tick == 20);
    assert(std::strcmp(heal[0].category.asChars(), "healing") == 0);
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — record_healing\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  21. CombatTelemetryHook — record_kill
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    hook.record_kill(1, 2, 30);

    auto kills = mc.samples_by_name(zs::SmallString{"kill"});
    assert(kills.size() == 1);
    assert(kills[0].entityId == 1);

    auto deaths = mc.samples_by_name(zs::SmallString{"death"});
    assert(deaths.size() == 1);
    assert(deaths[0].entityId == 2);
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — record_kill\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  22. CombatTelemetryHook — record_ability_use
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook hook(&mc);
    hook.record_ability_use(3, 101, 40);

    auto uses = mc.samples_by_name(zs::SmallString{"ability_use"});
    assert(uses.size() == 1);
    assert(uses[0].entityId == 3);
    assert(near(uses[0].value, 101.0));
    assert(uses[0].tick == 40);
    assert(std::strcmp(uses[0].category.asChars(), "ability") == 0);
    fprintf(stderr, "  [PASS] %d. CombatTelemetryHook — record_ability_use\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  23. ProgressionTelemetryHook — null collector safety
  // -----------------------------------------------------------------------
  {
    zs::ProgressionTelemetryHook hook(nullptr);
    hook.record_xp_award(1, 100);
    hook.record_level_up(1, 2);
    hook.record_skill_unlock(1, 10);
    assert(hook.collector() == nullptr);
    fprintf(stderr, "  [PASS] %d. ProgressionTelemetryHook — null collector safety\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  24. ProgressionTelemetryHook — record_xp_award
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::ProgressionTelemetryHook hook(&mc);
    hook.record_xp_award(7, 500, 50);

    auto xp = mc.samples_by_name(zs::SmallString{"xp_awarded"});
    assert(xp.size() == 1);
    assert(near(xp[0].value, 500.0));
    assert(xp[0].entityId == 7);
    assert(xp[0].tick == 50);
    assert(std::strcmp(xp[0].category.asChars(), "progression") == 0);
    fprintf(stderr, "  [PASS] %d. ProgressionTelemetryHook — record_xp_award\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  25. ProgressionTelemetryHook — record_level_up
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::ProgressionTelemetryHook hook(&mc);
    hook.record_level_up(7, 5, 60);

    auto lvl = mc.samples_by_name(zs::SmallString{"level_up"});
    assert(lvl.size() == 1);
    assert(near(lvl[0].value, 5.0));
    assert(lvl[0].entityId == 7);
    fprintf(stderr, "  [PASS] %d. ProgressionTelemetryHook — record_level_up\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  26. ProgressionTelemetryHook — record_skill_unlock
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::ProgressionTelemetryHook hook(&mc);
    hook.record_skill_unlock(7, 42, 70);

    auto sk = mc.samples_by_name(zs::SmallString{"skill_unlock"});
    assert(sk.size() == 1);
    assert(near(sk[0].value, 42.0));
    assert(sk[0].entityId == 7);
    fprintf(stderr, "  [PASS] %d. ProgressionTelemetryHook — record_skill_unlock\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  27. BalanceAnalyzer — register_threshold
  // -----------------------------------------------------------------------
  {
    zs::BalanceAnalyzer ba;
    assert(ba.threshold_count() == 0);

    zs::BalanceThreshold t;
    t.metricName = zs::SmallString{"dps"};
    t.expectedMin = 50.0;
    t.expectedMax = 150.0;
    t.description = zs::SmallString{"DPS should be in 50-150 range"};
    ba.register_threshold(t);
    assert(ba.threshold_count() == 1);

    ba.clear_thresholds();
    assert(ba.threshold_count() == 0);
    fprintf(stderr, "  [PASS] %d. BalanceAnalyzer — register_threshold\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  28. BalanceAnalyzer — analyze pass
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"dps"}, 90.0);
    mc.record(zs::SmallString{"dps"}, 110.0);
    mc.record(zs::SmallString{"dps"}, 100.0);
    // mean = 100, within [50, 150]

    zs::BalanceAnalyzer ba;
    zs::BalanceThreshold t;
    t.metricName = zs::SmallString{"dps"};
    t.expectedMin = 50.0;
    t.expectedMax = 150.0;
    ba.register_threshold(t);

    auto report = ba.analyze(mc);
    assert(report.records.size() == 1);
    assert(report.records[0].outcome == zs::ValidationOutcome::pass);
    assert(report.summary.total == 1);
    assert(report.summary.passed == 1);
    assert(report.summary.failed == 0);
    fprintf(stderr, "  [PASS] %d. BalanceAnalyzer — analyze pass\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  29. BalanceAnalyzer — analyze fail
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"dps"}, 200.0);
    mc.record(zs::SmallString{"dps"}, 250.0);
    // mean = 225, outside [50, 150]

    zs::BalanceAnalyzer ba;
    zs::BalanceThreshold t;
    t.metricName = zs::SmallString{"dps"};
    t.expectedMin = 50.0;
    t.expectedMax = 150.0;
    ba.register_threshold(t);

    auto report = ba.analyze(mc);
    assert(report.records.size() == 1);
    assert(report.records[0].outcome == zs::ValidationOutcome::fail);
    assert(report.summary.failed == 1);
    fprintf(stderr, "  [PASS] %d. BalanceAnalyzer — analyze fail\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  30. BalanceAnalyzer — analyze skip (no data)
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    // No data for "dps"

    zs::BalanceAnalyzer ba;
    zs::BalanceThreshold t;
    t.metricName = zs::SmallString{"dps"};
    t.expectedMin = 50.0;
    t.expectedMax = 150.0;
    ba.register_threshold(t);

    auto report = ba.analyze(mc);
    assert(report.records.size() == 1);
    assert(report.records[0].outcome == zs::ValidationOutcome::skip);
    assert(report.summary.skipped == 1);
    fprintf(stderr, "  [PASS] %d. BalanceAnalyzer — analyze skip (no data)\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  31. BalanceAnalyzer — summarize_all
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"dmg"}, 10.0);
    mc.record(zs::SmallString{"dmg"}, 20.0);
    mc.record(zs::SmallString{"heal"}, 5.0);

    zs::BalanceAnalyzer ba;
    auto summaries = ba.summarize_all(mc);
    assert(summaries.size() == 2);

    // Find the dmg summary
    bool foundDmg = false;
    bool foundHeal = false;
    for (auto &s : summaries) {
      if (std::strcmp(s.metricName.asChars(), "dmg") == 0) {
        assert(s.count == 2);
        assert(near(s.mean, 15.0));
        foundDmg = true;
      }
      if (std::strcmp(s.metricName.asChars(), "heal") == 0) {
        assert(s.count == 1);
        assert(near(s.mean, 5.0));
        foundHeal = true;
      }
    }
    assert(foundDmg);
    assert(foundHeal);
    fprintf(stderr, "  [PASS] %d. BalanceAnalyzer — summarize_all\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  32. SimulationSummaryBuilder — null collector
  // -----------------------------------------------------------------------
  {
    zs::SimulationSummaryBuilder builder;
    auto report = builder.build();
    assert(report.records.empty());
    assert(report.summary.total == 0);
    fprintf(stderr, "  [PASS] %d. SimulationSummaryBuilder — null collector\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  33. SimulationSummaryBuilder — build with data
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"damage"}, 100.0);
    mc.record(zs::SmallString{"damage"}, 200.0);
    mc.record(zs::SmallString{"healing"}, 50.0);

    zs::SimulationSummaryBuilder builder;
    builder.set_collector(&mc);
    auto report = builder.build(zs::SmallString{"test_sim"});

    assert(std::strcmp(report.suite.asChars(), "test_sim") == 0);
    assert(report.records.size() == 2);  // damage + healing

    // All records should be benchmark kind
    for (auto &r : report.records) {
      assert(r.kind == zs::ValidationRecordKind::benchmark);
      assert(r.outcome == zs::ValidationOutcome::pass);
      assert(!r.measurements.empty());
    }

    // Check that damage record has correct mean
    bool foundDmg = false;
    for (auto &r : report.records) {
      if (std::strcmp(r.name.asChars(), "damage") == 0) {
        // First measurement is "mean"
        assert(std::strcmp(r.measurements[0].name.asChars(), "mean") == 0);
        assert(near(r.measurements[0].value, 150.0));
        // Second is "count"
        assert(std::strcmp(r.measurements[1].name.asChars(), "count") == 0);
        assert(near(r.measurements[1].value, 2.0));
        // Third is "sum"
        assert(std::strcmp(r.measurements[2].name.asChars(), "sum") == 0);
        assert(near(r.measurements[2].value, 300.0));
        foundDmg = true;
      }
    }
    assert(foundDmg);

    // Summary should count all as passed
    assert(report.summary.total == 2);
    assert(report.summary.passed == 2);
    fprintf(stderr, "  [PASS] %d. SimulationSummaryBuilder — build with data\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  34. format_summary — text output
  // -----------------------------------------------------------------------
  {
    auto s = zs::StatisticalSummary::compute(zs::SmallString{"test_metric"},
               {10.0, 20.0, 30.0});
    auto text = zs::format_summary(s);
    // Should contain the metric name and key stats
    assert(text.find("test_metric") != std::string::npos);
    assert(text.find("n=3") != std::string::npos);
    assert(text.find("mean=") != std::string::npos);
    fprintf(stderr, "  [PASS] %d. format_summary — text output\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  35. summary_to_json — JSON output
  // -----------------------------------------------------------------------
  {
    auto s = zs::StatisticalSummary::compute(zs::SmallString{"test_metric"},
               {10.0, 20.0, 30.0});
    auto json = zs::summary_to_json(s);
    assert(json.find("\"metric\":\"test_metric\"") != std::string::npos);
    assert(json.find("\"count\":3") != std::string::npos);
    assert(json.find("\"mean\":") != std::string::npos);
    assert(json.find("\"stddev\":") != std::string::npos);
    assert(json.find("\"variance\":") != std::string::npos);
    // Should be valid-ish JSON (starts with { ends with })
    assert(json.front() == '{');
    assert(json.back() == '}');
    fprintf(stderr, "  [PASS] %d. summary_to_json — JSON output\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  36. summaries_to_json — JSON array
  // -----------------------------------------------------------------------
  {
    auto s1 = zs::StatisticalSummary::compute(zs::SmallString{"alpha"}, {1.0, 2.0});
    auto s2 = zs::StatisticalSummary::compute(zs::SmallString{"beta"}, {3.0, 4.0});
    std::vector<zs::StatisticalSummary> summaries = {s1, s2};
    auto json = zs::summaries_to_json(summaries);
    assert(json.front() == '[');
    assert(json.back() == ']');
    assert(json.find("\"alpha\"") != std::string::npos);
    assert(json.find("\"beta\"") != std::string::npos);
    fprintf(stderr, "  [PASS] %d. summaries_to_json — JSON array\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  37. balance_report_to_json — full report
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    mc.record(zs::SmallString{"dps"}, 100.0);
    mc.record(zs::SmallString{"dps"}, 120.0);

    zs::BalanceAnalyzer ba;
    zs::BalanceThreshold t;
    t.metricName = zs::SmallString{"dps"};
    t.expectedMin = 50.0;
    t.expectedMax = 150.0;
    t.description = zs::SmallString{"DPS range check"};
    ba.register_threshold(t);

    auto report = ba.analyze(mc);
    auto json = zs::balance_report_to_json(report);

    assert(json.front() == '{');
    assert(json.back() == '}');
    assert(json.find("\"suite\":\"gameplay_balance\"") != std::string::npos);
    assert(json.find("\"passed\":1") != std::string::npos);
    assert(json.find("\"outcome\":\"pass\"") != std::string::npos);
    assert(json.find("\"accepted\":true") != std::string::npos);
    fprintf(stderr, "  [PASS] %d. balance_report_to_json — full report\n", ++testNum);
  }

  // -----------------------------------------------------------------------
  //  38. Integration: multi-round combat telemetry + balance check
  // -----------------------------------------------------------------------
  {
    zs::MetricCollector mc;
    zs::CombatTelemetryHook combatHook(&mc);
    zs::ProgressionTelemetryHook progHook(&mc);

    // Simulate 10 rounds of combat
    for (zs::u64 tick = 0; tick < 10; ++tick) {
      // Attacker hits defender
      auto r = make_hit(1, 2, 100.0, 80.0 + static_cast<double>(tick));
      combatHook.record_combat_result(r, tick);
    }

    // Some healing
    combatHook.record_healing(2, 50.0, 5);
    combatHook.record_healing(2, 30.0, 8);

    // Kill event
    combatHook.record_kill(1, 2, 10);

    // Progression
    progHook.record_xp_award(1, 1000, 10);
    progHook.record_level_up(1, 2, 10);

    // Verify metric collection
    auto dmgVals = mc.values_for(zs::SmallString{"damage_dealt"});
    assert(dmgVals.size() == 10);

    auto healVals = mc.values_for(zs::SmallString{"heal_amount"});
    assert(healVals.size() == 2);

    // Balance check: damage_dealt mean should be around 84.5
    // (80 + 81 + ... + 89) / 10 = 84.5
    zs::BalanceAnalyzer ba;
    zs::BalanceThreshold dmgThreshold;
    dmgThreshold.metricName = zs::SmallString{"damage_dealt"};
    dmgThreshold.expectedMin = 70.0;
    dmgThreshold.expectedMax = 100.0;
    ba.register_threshold(dmgThreshold);

    zs::BalanceThreshold healThreshold;
    healThreshold.metricName = zs::SmallString{"heal_amount"};
    healThreshold.expectedMin = 30.0;
    healThreshold.expectedMax = 60.0;
    ba.register_threshold(healThreshold);

    auto report = ba.analyze(mc);
    assert(report.summary.total == 2);
    assert(report.summary.passed == 2);

    // Build simulation summary
    zs::SimulationSummaryBuilder builder;
    builder.set_collector(&mc);
    auto simReport = builder.build();
    assert(simReport.records.size() >= 5);  // damage, hit, mitigated, heal, kill, death, xp, level, etc.

    // Export to JSON
    auto json = zs::balance_report_to_json(report);
    assert(!json.empty());
    assert(json.find("\"passed\":2") != std::string::npos);

    fprintf(stderr, "  [PASS] %d. Integration: multi-round combat telemetry + balance check\n", ++testNum);
  }

  fprintf(stderr, "\nAll %d telemetry tests passed.\n", testNum);
  return 0;
}
