/// M6: Combat Resolution Pipeline - Unit Tests
///
/// Tests the CombatPipeline, default stage handlers, damage/healing
/// resolution, mitigation, status effect application, combat events,
/// and a two-entity combat scenario.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "zensim/gameplay/GameplayCombat.hpp"

/// Helper: approximate floating-point equality.
static bool approx(double a, double b, double eps = 0.001) {
  return std::fabs(a - b) < eps;
}

/// ---- Test 1: CombatOutcome and DamageType enums ----
static void test_enums() {
  fprintf(stderr, "[combat] test_enums...\n");

  assert(combat_outcome_name(zs::CombatOutcome::hit) != nullptr);
  assert(combat_outcome_name(zs::CombatOutcome::miss) != nullptr);
  assert(combat_outcome_name(zs::CombatOutcome::blocked) != nullptr);
  assert(combat_outcome_name(zs::CombatOutcome::critical) != nullptr);
  assert(combat_outcome_name(zs::CombatOutcome::evaded) != nullptr);
  assert(combat_outcome_name(zs::CombatOutcome::immune) != nullptr);

  assert(damage_type_name(zs::DamageType::physical) != nullptr);
  assert(damage_type_name(zs::DamageType::magical) != nullptr);
  assert(damage_type_name(zs::DamageType::pure) != nullptr);
  assert(damage_type_name(zs::DamageType::healing) != nullptr);

  assert(combat_stage_name(zs::CombatStage::pre_calculation) != nullptr);
  assert(combat_stage_name(zs::CombatStage::damage_calculation) != nullptr);
  assert(combat_stage_name(zs::CombatStage::mitigation) != nullptr);
  assert(combat_stage_name(zs::CombatStage::post_calculation) != nullptr);
  assert(combat_stage_name(zs::CombatStage::effect_application) != nullptr);
  assert(combat_stage_name(zs::CombatStage::event_emission) != nullptr);

  fprintf(stderr, "[combat] test_enums PASS\n");
}

/// ---- Test 2: CombatAction construction ----
static void test_action_construction() {
  fprintf(stderr, "[combat] test_action_construction...\n");

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.damageType = zs::DamageType::physical;
  action.actionName = zs::SmallString{"Slash"};
  action.basePower = 50.0;
  action.hitChance = 0.95;
  action.critChance = 0.1;
  action.critMultiplier = 2.0;

  assert(action.attackerId.value == 1);
  assert(action.defenderId.value == 2);
  assert(!action.is_healing());
  assert(approx(action.basePower, 50.0));

  // Healing action
  zs::CombatAction healAction{};
  healAction.damageType = zs::DamageType::healing;
  assert(healAction.is_healing());

  fprintf(stderr, "[combat] test_action_construction PASS\n");
}

/// ---- Test 3: CombatResult net_value ----
static void test_result_net_value() {
  fprintf(stderr, "[combat] test_result_net_value...\n");

  zs::CombatResult dmgResult{};
  dmgResult.damageType = zs::DamageType::physical;
  dmgResult.finalDamage = 30.0;
  assert(approx(dmgResult.net_value(), 30.0));

  zs::CombatResult healResult{};
  healResult.damageType = zs::DamageType::healing;
  healResult.finalHealing = 25.0;
  assert(approx(healResult.net_value(), -25.0));

  fprintf(stderr, "[combat] test_result_net_value PASS\n");
}

/// ---- Test 4: Pipeline with no stages ----
static void test_empty_pipeline() {
  fprintf(stderr, "[combat] test_empty_pipeline...\n");

  zs::CombatPipeline pipeline{};
  assert(pipeline.stage_count() == 0);

  zs::CombatAction action{};
  action.basePower = 100.0;
  auto result = pipeline.resolve(action);
  // No stages: result should have default values
  assert(approx(result.rawDamage, 0.0));
  assert(approx(result.finalDamage, 0.0));

  fprintf(stderr, "[combat] test_empty_pipeline PASS\n");
}

/// ---- Test 5: Pipeline with defaults - basic hit ----
static void test_default_pipeline_basic_hit() {
  fprintf(stderr, "[combat] test_default_pipeline_basic_hit...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  assert(pipeline.stage_count() == 6);

  // Deterministic: roll=0.0 means hit, no block, no crit
  pipeline.set_deterministic_roll(0.0);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 100.0;
  action.hitChance = 0.9;
  action.critChance = 0.0;  // No crit possible
  action.blockChance = 0.0; // No block possible
  action.damageType = zs::DamageType::pure; // Pure bypasses mitigation

  auto result = pipeline.resolve(action);
  assert(result.outcome == zs::CombatOutcome::hit);
  assert(!result.isMiss);
  assert(!result.isCritical);
  assert(!result.isBlocked);
  assert(approx(result.rawDamage, 100.0));
  assert(approx(result.finalDamage, 100.0));
  assert(approx(result.mitigatedAmount, 0.0));

  fprintf(stderr, "[combat] test_default_pipeline_basic_hit PASS\n");
}

/// ---- Test 6: Pipeline miss ----
static void test_default_pipeline_miss() {
  fprintf(stderr, "[combat] test_default_pipeline_miss...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  // Roll = 0.95 which is >= hitChance(0.9) => miss
  pipeline.set_deterministic_roll(0.95);

  zs::CombatAction action{};
  action.basePower = 100.0;
  action.hitChance = 0.9;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(result.outcome == zs::CombatOutcome::miss);
  assert(result.isMiss);
  assert(approx(result.rawDamage, 0.0));
  assert(approx(result.finalDamage, 0.0));

  fprintf(stderr, "[combat] test_default_pipeline_miss PASS\n");
}

/// ---- Test 7: Pipeline critical hit ----
static void test_default_pipeline_critical() {
  fprintf(stderr, "[combat] test_default_pipeline_critical...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  // Roll=0.0: hit check passes (0.0 < 1.0), block check passes (0.0 >= 0.0),
  // crit check passes (0.0 < 0.5)
  pipeline.set_deterministic_roll(0.0);

  zs::CombatAction action{};
  action.basePower = 100.0;
  action.hitChance = 1.0;
  action.blockChance = 0.0;
  action.critChance = 0.5;
  action.critMultiplier = 2.5;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(result.outcome == zs::CombatOutcome::critical);
  assert(result.isCritical);
  assert(!result.isMiss);
  // Raw damage: 100.0 * 2.5 = 250.0
  assert(approx(result.rawDamage, 250.0));
  assert(approx(result.finalDamage, 250.0));

  fprintf(stderr, "[combat] test_default_pipeline_critical PASS\n");
}

/// ---- Test 8: Pipeline blocked attack ----
static void test_default_pipeline_blocked() {
  fprintf(stderr, "[combat] test_default_pipeline_blocked...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  // Roll=0.0: hit passes, block passes (0.0 < 1.0)
  pipeline.set_deterministic_roll(0.0);

  zs::CombatAction action{};
  action.basePower = 100.0;
  action.hitChance = 1.0;
  action.blockChance = 1.0;  // Always block
  action.critChance = 0.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(result.outcome == zs::CombatOutcome::blocked);
  assert(result.isBlocked);
  // Blocked: 50% damage reduction => 50.0
  assert(approx(result.rawDamage, 50.0));
  assert(approx(result.finalDamage, 50.0));

  fprintf(stderr, "[combat] test_default_pipeline_blocked PASS\n");
}

/// ---- Test 9: Physical damage with armor mitigation ----
static void test_physical_mitigation() {
  fprintf(stderr, "[combat] test_physical_mitigation...\n");

  zs::EffectSystem effectSys{};
  auto &defenderStats = effectSys.stat_block(zs::GameplayEntityId{2});
  defenderStats.set_base(zs::SmallString{"armor"}, 50.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 100.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::physical;

  auto result = pipeline.resolve(action);
  assert(!result.isMiss);
  assert(approx(result.rawDamage, 100.0));
  // Mitigation: armor / (armor + 100) = 50 / 150 = 0.3333
  // Mitigated: 100.0 * 0.3333 = 33.33
  // Final: 100.0 - 33.33 = 66.67
  assert(approx(result.mitigatedAmount, 33.333, 0.01));
  assert(approx(result.finalDamage, 66.667, 0.01));

  fprintf(stderr, "[combat] test_physical_mitigation PASS\n");
}

/// ---- Test 10: Magical damage with resistance mitigation ----
static void test_magical_mitigation() {
  fprintf(stderr, "[combat] test_magical_mitigation...\n");

  zs::EffectSystem effectSys{};
  auto &defenderStats = effectSys.stat_block(zs::GameplayEntityId{2});
  defenderStats.set_base(zs::SmallString{"magic_resistance"}, 100.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 200.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::magical;

  auto result = pipeline.resolve(action);
  // Mitigation: 100 / (100 + 100) = 0.5
  // Mitigated: 200 * 0.5 = 100
  // Final: 200 - 100 = 100
  assert(approx(result.rawDamage, 200.0));
  assert(approx(result.mitigatedAmount, 100.0));
  assert(approx(result.finalDamage, 100.0));

  fprintf(stderr, "[combat] test_magical_mitigation PASS\n");
}

/// ---- Test 11: Pure damage bypasses mitigation ----
static void test_pure_damage() {
  fprintf(stderr, "[combat] test_pure_damage...\n");

  zs::EffectSystem effectSys{};
  auto &defenderStats = effectSys.stat_block(zs::GameplayEntityId{2});
  defenderStats.set_base(zs::SmallString{"armor"}, 999.0);
  defenderStats.set_base(zs::SmallString{"magic_resistance"}, 999.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 75.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(approx(result.rawDamage, 75.0));
  assert(approx(result.mitigatedAmount, 0.0));
  assert(approx(result.finalDamage, 75.0));

  fprintf(stderr, "[combat] test_pure_damage PASS\n");
}

/// ---- Test 12: Healing resolution ----
static void test_healing_resolution() {
  fprintf(stderr, "[combat] test_healing_resolution...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 80.0;
  action.damageType = zs::DamageType::healing;

  auto result = pipeline.resolve(action);
  assert(!result.isMiss);
  // Healing bypasses mitigation
  assert(approx(result.rawHealing, 80.0));
  assert(approx(result.finalHealing, 80.0));
  assert(approx(result.finalDamage, 0.0));
  // Net value for healing should be negative
  assert(approx(result.net_value(), -80.0));

  fprintf(stderr, "[combat] test_healing_resolution PASS\n");
}

/// ---- Test 13: Healing with healing_power stat scaling ----
static void test_healing_with_stat_scaling() {
  fprintf(stderr, "[combat] test_healing_with_stat_scaling...\n");

  zs::EffectSystem effectSys{};
  auto &attackerStats = effectSys.stat_block(zs::GameplayEntityId{1});
  attackerStats.set_base(zs::SmallString{"healing_power"}, 50.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 100.0;
  action.damageType = zs::DamageType::healing;

  auto result = pipeline.resolve(action);
  // Raw healing: 100.0 * (1.0 + 50.0/100.0) = 100.0 * 1.5 = 150.0
  assert(approx(result.rawHealing, 150.0));
  assert(approx(result.finalHealing, 150.0));

  fprintf(stderr, "[combat] test_healing_with_stat_scaling PASS\n");
}

/// ---- Test 14: Damage with attack_power stat scaling ----
static void test_damage_with_attack_power() {
  fprintf(stderr, "[combat] test_damage_with_attack_power...\n");

  zs::EffectSystem effectSys{};
  auto &attackerStats = effectSys.stat_block(zs::GameplayEntityId{1});
  attackerStats.set_base(zs::SmallString{"attack_power"}, 100.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 50.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  // Raw: 50 * (1.0 + 100/100) = 50 * 2.0 = 100.0
  assert(approx(result.rawDamage, 100.0));
  assert(approx(result.finalDamage, 100.0));

  fprintf(stderr, "[combat] test_damage_with_attack_power PASS\n");
}

/// ---- Test 15: Combat events emission ----
static void test_combat_events() {
  fprintf(stderr, "[combat] test_combat_events...\n");

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(100);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_dispatcher(&dispatcher);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 50.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(!result.isMiss);

  // Should have emitted damage_dealt and damage_received events
  auto &history = dispatcher.history();
  assert(history.size() >= 2);

  bool foundDealt = false;
  bool foundRecv = false;
  for (const auto &evt : history) {
    if (evt.typeId == zs::combat_events::damage_dealt) foundDealt = true;
    if (evt.typeId == zs::combat_events::damage_received) foundRecv = true;
  }
  assert(foundDealt);
  assert(foundRecv);

  fprintf(stderr, "[combat] test_combat_events PASS\n");
}

/// ---- Test 16: Healing events emission ----
static void test_healing_events() {
  fprintf(stderr, "[combat] test_healing_events...\n");

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(100);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_dispatcher(&dispatcher);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 40.0;
  action.damageType = zs::DamageType::healing;

  pipeline.resolve(action);

  auto &history = dispatcher.history();
  bool foundHealDealt = false;
  bool foundHealRecv = false;
  for (const auto &evt : history) {
    if (evt.typeId == zs::combat_events::healing_dealt) foundHealDealt = true;
    if (evt.typeId == zs::combat_events::healing_received) foundHealRecv = true;
  }
  assert(foundHealDealt);
  assert(foundHealRecv);

  fprintf(stderr, "[combat] test_healing_events PASS\n");
}

/// ---- Test 17: Miss event emission ----
static void test_miss_event() {
  fprintf(stderr, "[combat] test_miss_event...\n");

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(100);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.95);
  pipeline.set_dispatcher(&dispatcher);

  zs::CombatAction action{};
  action.basePower = 50.0;
  action.hitChance = 0.9;
  action.damageType = zs::DamageType::physical;

  pipeline.resolve(action);

  auto &history = dispatcher.history();
  bool foundMiss = false;
  for (const auto &evt : history) {
    if (evt.typeId == zs::combat_events::attack_missed) foundMiss = true;
  }
  assert(foundMiss);

  fprintf(stderr, "[combat] test_miss_event PASS\n");
}

/// ---- Test 18: Critical hit event emission ----
static void test_critical_event() {
  fprintf(stderr, "[combat] test_critical_event...\n");

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(100);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_dispatcher(&dispatcher);

  zs::CombatAction action{};
  action.basePower = 50.0;
  action.hitChance = 1.0;
  action.blockChance = 0.0;
  action.critChance = 0.5;
  action.damageType = zs::DamageType::pure;

  pipeline.resolve(action);

  auto &history = dispatcher.history();
  bool foundCrit = false;
  for (const auto &evt : history) {
    if (evt.typeId == zs::combat_events::attack_critical) foundCrit = true;
  }
  assert(foundCrit);

  fprintf(stderr, "[combat] test_critical_event PASS\n");
}

/// ---- Test 19: On-hit effect application ----
static void test_on_hit_effect_application() {
  fprintf(stderr, "[combat] test_on_hit_effect_application...\n");

  zs::EffectSystem effectSys{};

  // Register a poison effect
  zs::EffectDescriptor poison{};
  poison.id = zs::EffectDescriptorId{10};
  poison.name = zs::SmallString{"Poison"};
  poison.durationType = zs::EffectDurationType::duration;
  poison.duration = 10.0;
  poison.magnitude = 5.0;
  effectSys.register_descriptor(poison);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 50.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;
  action.onHitEffects.push_back(zs::EffectDescriptorId{10});

  auto result = pipeline.resolve(action);
  assert(!result.isMiss);
  assert(result.appliedEffects.size() == 1);
  assert(result.appliedEffects[0].valid());

  // Verify the effect is now active on the defender
  assert(effectSys.has_effect(zs::GameplayEntityId{2}, zs::EffectDescriptorId{10}));

  fprintf(stderr, "[combat] test_on_hit_effect_application PASS\n");
}

/// ---- Test 20: On-hit effect not applied on miss ----
static void test_on_hit_effect_no_apply_on_miss() {
  fprintf(stderr, "[combat] test_on_hit_effect_no_apply_on_miss...\n");

  zs::EffectSystem effectSys{};

  zs::EffectDescriptor burn{};
  burn.id = zs::EffectDescriptorId{11};
  burn.name = zs::SmallString{"Burn"};
  burn.durationType = zs::EffectDurationType::duration;
  burn.duration = 5.0;
  effectSys.register_descriptor(burn);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.95);  // miss
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 50.0;
  action.hitChance = 0.9;
  action.damageType = zs::DamageType::physical;
  action.onHitEffects.push_back(zs::EffectDescriptorId{11});

  auto result = pipeline.resolve(action);
  assert(result.isMiss);
  assert(result.appliedEffects.empty());
  assert(!effectSys.has_effect(zs::GameplayEntityId{2}, zs::EffectDescriptorId{11}));

  fprintf(stderr, "[combat] test_on_hit_effect_no_apply_on_miss PASS\n");
}

/// ---- Test 21: Custom stage handler ----
static void test_custom_stage_handler() {
  fprintf(stderr, "[combat] test_custom_stage_handler...\n");

  zs::CombatPipeline pipeline{};

  // Register only a custom damage calculation stage
  pipeline.register_stage(zs::CombatStage::damage_calculation,
                          zs::SmallString{"custom_dmg"},
                          [](zs::CombatContext &ctx) -> bool {
                            ctx.result.rawDamage = 42.0;
                            ctx.result.finalDamage = 42.0;
                            return true;
                          });

  zs::CombatAction action{};
  action.basePower = 999.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(approx(result.rawDamage, 42.0));
  assert(approx(result.finalDamage, 42.0));

  fprintf(stderr, "[combat] test_custom_stage_handler PASS\n");
}

/// ---- Test 22: Stage priority ordering ----
static void test_stage_priority_ordering() {
  fprintf(stderr, "[combat] test_stage_priority_ordering...\n");

  zs::CombatPipeline pipeline{};
  std::vector<int> order{};

  // Register two handlers for the same stage with different priorities
  pipeline.register_stage(zs::CombatStage::post_calculation,
                          zs::SmallString{"second"},
                          [&order](zs::CombatContext &) -> bool {
                            order.push_back(2);
                            return true;
                          }, 10);
  pipeline.register_stage(zs::CombatStage::post_calculation,
                          zs::SmallString{"first"},
                          [&order](zs::CombatContext &) -> bool {
                            order.push_back(1);
                            return true;
                          }, 5);

  zs::CombatAction action{};
  pipeline.resolve(action);

  assert(order.size() == 2);
  assert(order[0] == 1);  // Lower priority runs first
  assert(order[1] == 2);

  fprintf(stderr, "[combat] test_stage_priority_ordering PASS\n");
}

/// ---- Test 23: Pipeline abort (short-circuit) ----
static void test_pipeline_abort() {
  fprintf(stderr, "[combat] test_pipeline_abort...\n");

  zs::CombatPipeline pipeline{};
  bool secondRan = false;

  pipeline.register_stage(zs::CombatStage::pre_calculation,
                          zs::SmallString{"abort"},
                          [](zs::CombatContext &ctx) -> bool {
                            ctx.result.outcome = zs::CombatOutcome::immune;
                            return false;  // Abort pipeline
                          });
  pipeline.register_stage(zs::CombatStage::damage_calculation,
                          zs::SmallString{"should_not_run"},
                          [&secondRan](zs::CombatContext &) -> bool {
                            secondRan = true;
                            return true;
                          });

  zs::CombatAction action{};
  auto result = pipeline.resolve(action);

  assert(result.outcome == zs::CombatOutcome::immune);
  assert(!secondRan);

  fprintf(stderr, "[combat] test_pipeline_abort PASS\n");
}

/// ---- Test 24: Replace stage by name ----
static void test_replace_stage() {
  fprintf(stderr, "[combat] test_replace_stage...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  assert(pipeline.stage_count() == 6);

  // Replace the default damage calculation
  bool replaced = pipeline.replace_stage(zs::SmallString{"default_dmg_calc"},
      [](zs::CombatContext &ctx) -> bool {
        ctx.result.rawDamage = 999.0;
        ctx.result.finalDamage = 999.0;
        return true;
      });
  assert(replaced);

  pipeline.set_deterministic_roll(0.0);

  zs::CombatAction action{};
  action.basePower = 10.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  // Custom stage sets 999 regardless of basePower
  assert(approx(result.finalDamage, 999.0));

  fprintf(stderr, "[combat] test_replace_stage PASS\n");
}

/// ---- Test 25: Remove stage by name ----
static void test_remove_stage_by_name() {
  fprintf(stderr, "[combat] test_remove_stage_by_name...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  assert(pipeline.stage_count() == 6);

  bool removed = pipeline.remove_stage_by_name(zs::SmallString{"default_mitigation"});
  assert(removed);
  assert(pipeline.stage_count() == 5);

  // Verify the non-existent name returns false
  assert(!pipeline.remove_stage_by_name(zs::SmallString{"nonexistent"}));

  fprintf(stderr, "[combat] test_remove_stage_by_name PASS\n");
}

/// ---- Test 26: Remove all stages of a type ----
static void test_remove_stage_type() {
  fprintf(stderr, "[combat] test_remove_stage_type...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();

  // Add a second handler for the same stage
  pipeline.register_stage(zs::CombatStage::mitigation,
                          zs::SmallString{"extra_mit"},
                          [](zs::CombatContext &) -> bool { return true; });
  assert(pipeline.stage_count() == 7);

  size_t removed = pipeline.remove_stage(zs::CombatStage::mitigation);
  assert(removed == 2);
  assert(pipeline.stage_count() == 5);

  fprintf(stderr, "[combat] test_remove_stage_type PASS\n");
}

/// ---- Test 27: Batch resolution ----
static void test_batch_resolution() {
  fprintf(stderr, "[combat] test_batch_resolution...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);

  std::vector<zs::CombatAction> actions{};
  for (int i = 0; i < 5; ++i) {
    zs::CombatAction a{};
    a.attackerId = zs::GameplayEntityId{1};
    a.defenderId = zs::GameplayEntityId{2};
    a.basePower = static_cast<double>(10 * (i + 1));
    a.hitChance = 1.0;
    a.critChance = 0.0;
    a.blockChance = 0.0;
    a.damageType = zs::DamageType::pure;
    actions.push_back(a);
  }

  auto results = pipeline.resolve_batch(actions);
  assert(results.size() == 5);
  assert(approx(results[0].finalDamage, 10.0));
  assert(approx(results[1].finalDamage, 20.0));
  assert(approx(results[2].finalDamage, 30.0));
  assert(approx(results[3].finalDamage, 40.0));
  assert(approx(results[4].finalDamage, 50.0));

  fprintf(stderr, "[combat] test_batch_resolution PASS\n");
}

/// ---- Test 28: HealthTracker basics ----
static void test_health_tracker_basics() {
  fprintf(stderr, "[combat] test_health_tracker_basics...\n");

  zs::HealthTracker tracker{};
  zs::GameplayEntityId entity{1};

  tracker.set_max_hp(entity, 100.0);
  assert(approx(tracker.max_hp(entity), 100.0));
  assert(approx(tracker.current_hp(entity), 100.0));
  assert(tracker.is_alive(entity));
  assert(tracker.entity_count() == 1);

  // Apply damage
  double actual = tracker.apply_damage(entity, 30.0);
  assert(approx(actual, 30.0));
  assert(approx(tracker.current_hp(entity), 70.0));

  // Apply healing
  actual = tracker.apply_healing(entity, 20.0);
  assert(approx(actual, 20.0));
  assert(approx(tracker.current_hp(entity), 90.0));

  // Over-heal: only heals up to max
  actual = tracker.apply_healing(entity, 50.0);
  assert(approx(actual, 10.0));
  assert(approx(tracker.current_hp(entity), 100.0));

  // Overkill: damage clamped to current HP
  actual = tracker.apply_damage(entity, 200.0);
  assert(approx(actual, 100.0));
  assert(approx(tracker.current_hp(entity), 0.0));
  assert(!tracker.is_alive(entity));

  fprintf(stderr, "[combat] test_health_tracker_basics PASS\n");
}

/// ---- Test 29: HealthTracker apply_result integration ----
static void test_health_tracker_apply_result() {
  fprintf(stderr, "[combat] test_health_tracker_apply_result...\n");

  zs::HealthTracker tracker{};
  tracker.set_max_hp(zs::GameplayEntityId{2}, 200.0);

  // Create a damage result
  zs::CombatResult dmgResult{};
  dmgResult.damageType = zs::DamageType::physical;
  dmgResult.finalDamage = 75.0;
  dmgResult.defenderId = zs::GameplayEntityId{2};

  double actual = tracker.apply_result(dmgResult);
  assert(approx(actual, 75.0));
  assert(approx(tracker.current_hp(zs::GameplayEntityId{2}), 125.0));

  // Create a heal result
  zs::CombatResult healResult{};
  healResult.damageType = zs::DamageType::healing;
  healResult.finalHealing = 50.0;
  healResult.defenderId = zs::GameplayEntityId{2};

  actual = tracker.apply_result(healResult);
  assert(approx(actual, 50.0));
  assert(approx(tracker.current_hp(zs::GameplayEntityId{2}), 175.0));

  fprintf(stderr, "[combat] test_health_tracker_apply_result PASS\n");
}

/// ---- Test 30: HealthTracker set_current_hp clamping ----
static void test_health_tracker_set_hp() {
  fprintf(stderr, "[combat] test_health_tracker_set_hp...\n");

  zs::HealthTracker tracker{};
  tracker.set_max_hp(zs::GameplayEntityId{1}, 100.0);

  tracker.set_current_hp(zs::GameplayEntityId{1}, 50.0);
  assert(approx(tracker.current_hp(zs::GameplayEntityId{1}), 50.0));

  // Clamp above max
  tracker.set_current_hp(zs::GameplayEntityId{1}, 200.0);
  assert(approx(tracker.current_hp(zs::GameplayEntityId{1}), 100.0));

  // Clamp below zero
  tracker.set_current_hp(zs::GameplayEntityId{1}, -50.0);
  assert(approx(tracker.current_hp(zs::GameplayEntityId{1}), 0.0));

  // Non-existent entity: no-op
  tracker.set_current_hp(zs::GameplayEntityId{999}, 50.0);
  assert(approx(tracker.current_hp(zs::GameplayEntityId{999}), 0.0));

  fprintf(stderr, "[combat] test_health_tracker_set_hp PASS\n");
}

/// ---- Test 31: Two-entity combat scenario (canary-style) ----
static void test_two_entity_combat_scenario() {
  fprintf(stderr, "[combat] test_two_entity_combat_scenario...\n");

  // Setup systems
  zs::EffectSystem effectSys{};
  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(200);
  zs::HealthTracker healthTracker{};

  // Entity IDs
  zs::GameplayEntityId warrior{1};
  zs::GameplayEntityId mage{2};

  // Setup stats
  auto &warriorStats = effectSys.stat_block(warrior);
  warriorStats.set_base(zs::SmallString{"attack_power"}, 50.0);
  warriorStats.set_base(zs::SmallString{"armor"}, 80.0);

  auto &mageStats = effectSys.stat_block(mage);
  mageStats.set_base(zs::SmallString{"attack_power"}, 30.0);
  mageStats.set_base(zs::SmallString{"magic_resistance"}, 40.0);

  // Setup health
  healthTracker.set_max_hp(warrior, 500.0);
  healthTracker.set_max_hp(mage, 300.0);

  // Register a bleed effect
  zs::EffectDescriptor bleed{};
  bleed.id = zs::EffectDescriptorId{20};
  bleed.name = zs::SmallString{"Bleed"};
  bleed.durationType = zs::EffectDurationType::periodic;
  bleed.duration = 6.0;
  bleed.period = 2.0;
  bleed.magnitude = 10.0;
  effectSys.register_descriptor(bleed);

  // Setup pipeline
  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);
  pipeline.set_dispatcher(&dispatcher);

  // Round 1: Warrior attacks Mage (physical)
  {
    zs::CombatAction action{};
    action.attackerId = warrior;
    action.defenderId = mage;
    action.basePower = 60.0;
    action.hitChance = 1.0;
    action.critChance = 0.0;
    action.blockChance = 0.0;
    action.damageType = zs::DamageType::physical;
    action.actionName = zs::SmallString{"Heavy Strike"};
    action.onHitEffects.push_back(zs::EffectDescriptorId{20}); // Apply bleed

    auto result = pipeline.resolve(action);
    assert(!result.isMiss);
    // attack_power 50 => raw = 60 * (1 + 50/100) = 60 * 1.5 = 90
    assert(approx(result.rawDamage, 90.0));
    // Mage has no armor -> mitigation = 0 for physical without armor stat
    // Actually mage has magic_resistance not armor, so physical mitigation=0
    assert(approx(result.finalDamage, 90.0));
    assert(result.appliedEffects.size() == 1);

    double actualDmg = healthTracker.apply_result(result);
    assert(approx(actualDmg, 90.0));
    assert(approx(healthTracker.current_hp(mage), 210.0));
  }

  // Round 2: Mage attacks Warrior (magical)
  {
    zs::CombatAction action{};
    action.attackerId = mage;
    action.defenderId = warrior;
    action.basePower = 80.0;
    action.hitChance = 1.0;
    action.critChance = 0.0;
    action.blockChance = 0.0;
    action.damageType = zs::DamageType::magical;
    action.actionName = zs::SmallString{"Fireball"};

    auto result = pipeline.resolve(action);
    assert(!result.isMiss);
    // attack_power 30 => raw = 80 * (1 + 30/100) = 80 * 1.3 = 104
    assert(approx(result.rawDamage, 104.0));
    // Warrior has no magic_resistance set, only armor
    // Wait, warrior has armor=80, but magical uses magic_resistance
    // Warrior doesn't have magic_resistance, so mitigation = 0
    assert(approx(result.finalDamage, 104.0));

    healthTracker.apply_result(result);
    assert(approx(healthTracker.current_hp(warrior), 396.0));
  }

  // Round 3: Warrior attacks Mage again (this time check bleed is still on)
  {
    assert(effectSys.has_effect(mage, zs::EffectDescriptorId{20}));

    zs::CombatAction action{};
    action.attackerId = warrior;
    action.defenderId = mage;
    action.basePower = 40.0;
    action.hitChance = 1.0;
    action.critChance = 0.0;
    action.blockChance = 0.0;
    action.damageType = zs::DamageType::pure;
    action.actionName = zs::SmallString{"Execute"};

    auto result = pipeline.resolve(action);
    // Raw: 40 * (1 + 50/100) = 40 * 1.5 = 60
    assert(approx(result.rawDamage, 60.0));
    assert(approx(result.finalDamage, 60.0));

    healthTracker.apply_result(result);
    assert(approx(healthTracker.current_hp(mage), 150.0));
  }

  // Verify events were emitted throughout
  auto &history = dispatcher.history();
  assert(history.size() >= 6);  // At least 2 events per round * 3 rounds

  // Verify both entities are still alive
  assert(healthTracker.is_alive(warrior));
  assert(healthTracker.is_alive(mage));

  // Verify the bleed effect is tracked
  assert(effectSys.effect_count(mage) == 1);

  fprintf(stderr, "[combat] test_two_entity_combat_scenario PASS\n");
}

/// ---- Test 32: Effect application with events during combat ----
static void test_effect_application_with_events() {
  fprintf(stderr, "[combat] test_effect_application_with_events...\n");

  zs::EffectSystem effectSys{};
  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(100);

  // Register a slow effect with stat modifier
  zs::EffectDescriptor slow{};
  slow.id = zs::EffectDescriptorId{30};
  slow.name = zs::SmallString{"Slow"};
  slow.durationType = zs::EffectDurationType::duration;
  slow.duration = 5.0;
  slow.magnitude = 1.0;
  zs::StatModifier speedMod{};
  speedMod.statName = zs::SmallString{"speed"};
  speedMod.operation = zs::StatModOp::multiplicative;
  speedMod.value = 0.5;  // 50% speed reduction
  slow.modifiers.push_back(speedMod);
  effectSys.register_descriptor(slow);

  // Set defender speed
  auto &defStats = effectSys.stat_block(zs::GameplayEntityId{2});
  defStats.set_base(zs::SmallString{"speed"}, 100.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);
  pipeline.set_dispatcher(&dispatcher);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 10.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;
  action.onHitEffects.push_back(zs::EffectDescriptorId{30});

  auto result = pipeline.resolve(action);
  assert(result.appliedEffects.size() == 1);

  // Verify slow effect modifies speed
  double speed = defStats.compute(zs::SmallString{"speed"});
  assert(approx(speed, 50.0));  // 100 * 0.5 = 50

  // Verify effect.applied event was emitted by EffectSystem
  bool foundEffectApplied = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::effect_events::applied) {
      foundEffectApplied = true;
      break;
    }
  }
  assert(foundEffectApplied);

  // Verify combat effect event was also emitted
  bool foundCombatEffect = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::combat_events::effect_applied_in_combat) {
      foundCombatEffect = true;
      break;
    }
  }
  assert(foundCombatEffect);

  fprintf(stderr, "[combat] test_effect_application_with_events PASS\n");
}

/// ---- Test 33: Pipeline with multiple handlers per stage ----
static void test_multiple_handlers_per_stage() {
  fprintf(stderr, "[combat] test_multiple_handlers_per_stage...\n");

  zs::CombatPipeline pipeline{};

  // Two damage calculation stages: one sets base, another adds bonus
  pipeline.register_stage(zs::CombatStage::damage_calculation,
                          zs::SmallString{"base_calc"},
                          [](zs::CombatContext &ctx) -> bool {
                            ctx.result.rawDamage = ctx.action.basePower;
                            return true;
                          }, 0);
  pipeline.register_stage(zs::CombatStage::damage_calculation,
                          zs::SmallString{"bonus_calc"},
                          [](zs::CombatContext &ctx) -> bool {
                            ctx.result.rawDamage += 25.0;  // Flat bonus
                            ctx.result.finalDamage = ctx.result.rawDamage;
                            return true;
                          }, 10);

  zs::CombatAction action{};
  action.basePower = 100.0;

  auto result = pipeline.resolve(action);
  assert(approx(result.rawDamage, 125.0));
  assert(approx(result.finalDamage, 125.0));

  fprintf(stderr, "[combat] test_multiple_handlers_per_stage PASS\n");
}

/// ---- Test 34: Clear stages ----
static void test_clear_stages() {
  fprintf(stderr, "[combat] test_clear_stages...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  assert(pipeline.stage_count() == 6);

  pipeline.clear_stages();
  assert(pipeline.stage_count() == 0);

  // Resolve with empty pipeline should be no-op
  zs::CombatAction action{};
  action.basePower = 100.0;
  auto result = pipeline.resolve(action);
  assert(approx(result.finalDamage, 0.0));

  fprintf(stderr, "[combat] test_clear_stages PASS\n");
}

/// ---- Test 35: HealthTracker clear ----
static void test_health_tracker_clear() {
  fprintf(stderr, "[combat] test_health_tracker_clear...\n");

  zs::HealthTracker tracker{};
  tracker.set_max_hp(zs::GameplayEntityId{1}, 100.0);
  tracker.set_max_hp(zs::GameplayEntityId{2}, 200.0);
  assert(tracker.entity_count() == 2);

  tracker.clear();
  assert(tracker.entity_count() == 0);
  assert(approx(tracker.current_hp(zs::GameplayEntityId{1}), 0.0));

  fprintf(stderr, "[combat] test_health_tracker_clear PASS\n");
}

/// ---- Test 36: Full combat to death scenario ----
static void test_combat_to_death() {
  fprintf(stderr, "[combat] test_combat_to_death...\n");

  zs::HealthTracker tracker{};
  tracker.set_max_hp(zs::GameplayEntityId{1}, 100.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);

  // Deal 120 damage to entity with 100 HP
  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{2};
  action.defenderId = zs::GameplayEntityId{1};
  action.basePower = 120.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;

  auto result = pipeline.resolve(action);
  assert(approx(result.finalDamage, 120.0));

  double actual = tracker.apply_result(result);
  // Only 100 HP to take
  assert(approx(actual, 100.0));
  assert(approx(tracker.current_hp(zs::GameplayEntityId{1}), 0.0));
  assert(!tracker.is_alive(zs::GameplayEntityId{1}));

  fprintf(stderr, "[combat] test_combat_to_death PASS\n");
}

/// ---- Test 37: Armor + attack_power + crit combined ----
static void test_combined_stats_and_crit() {
  fprintf(stderr, "[combat] test_combined_stats_and_crit...\n");

  zs::EffectSystem effectSys{};

  auto &attackerStats = effectSys.stat_block(zs::GameplayEntityId{1});
  attackerStats.set_base(zs::SmallString{"attack_power"}, 100.0);

  auto &defenderStats = effectSys.stat_block(zs::GameplayEntityId{2});
  defenderStats.set_base(zs::SmallString{"armor"}, 100.0);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 50.0;
  action.hitChance = 1.0;
  action.blockChance = 0.0;
  action.critChance = 1.0;    // Always crit
  action.critMultiplier = 2.0;
  action.damageType = zs::DamageType::physical;

  auto result = pipeline.resolve(action);
  assert(result.isCritical);
  // attack_power 100 => raw = 50 * (1 + 100/100) = 50 * 2.0 = 100
  // crit: 100 * 2.0 = 200
  assert(approx(result.rawDamage, 200.0));
  // armor 100 => mitigation = 100 / (100+100) = 0.5
  // mitigated = 200 * 0.5 = 100
  // final = 200 - 100 = 100
  assert(approx(result.mitigatedAmount, 100.0));
  assert(approx(result.finalDamage, 100.0));

  fprintf(stderr, "[combat] test_combined_stats_and_crit PASS\n");
}

/// ---- Test 38: Result preserves source data ----
static void test_result_source_data() {
  fprintf(stderr, "[combat] test_result_source_data...\n");

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{42};
  action.defenderId = zs::GameplayEntityId{99};
  action.actionName = zs::SmallString{"TestAction"};
  action.damageType = zs::DamageType::magical;
  action.basePower = 10.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;

  auto result = pipeline.resolve(action);
  assert(result.attackerId == zs::GameplayEntityId{42});
  assert(result.defenderId == zs::GameplayEntityId{99});
  assert(result.actionName == zs::SmallString{"TestAction"});
  assert(result.damageType == zs::DamageType::magical);

  fprintf(stderr, "[combat] test_result_source_data PASS\n");
}

/// ---- Test 39: Multiple on-hit effects ----
static void test_multiple_on_hit_effects() {
  fprintf(stderr, "[combat] test_multiple_on_hit_effects...\n");

  zs::EffectSystem effectSys{};

  zs::EffectDescriptor burn{};
  burn.id = zs::EffectDescriptorId{40};
  burn.name = zs::SmallString{"Burn"};
  burn.durationType = zs::EffectDurationType::duration;
  burn.duration = 5.0;
  effectSys.register_descriptor(burn);

  zs::EffectDescriptor frostbite{};
  frostbite.id = zs::EffectDescriptorId{41};
  frostbite.name = zs::SmallString{"Frostbite"};
  frostbite.durationType = zs::EffectDurationType::duration;
  frostbite.duration = 3.0;
  effectSys.register_descriptor(frostbite);

  zs::EffectDescriptor shock{};
  shock.id = zs::EffectDescriptorId{42};
  shock.name = zs::SmallString{"Shock"};
  shock.durationType = zs::EffectDurationType::duration;
  shock.duration = 4.0;
  effectSys.register_descriptor(shock);

  auto pipeline = zs::CombatPipeline::with_defaults();
  pipeline.set_deterministic_roll(0.0);
  pipeline.set_effect_system(&effectSys);

  zs::CombatAction action{};
  action.attackerId = zs::GameplayEntityId{1};
  action.defenderId = zs::GameplayEntityId{2};
  action.basePower = 10.0;
  action.hitChance = 1.0;
  action.critChance = 0.0;
  action.blockChance = 0.0;
  action.damageType = zs::DamageType::pure;
  action.onHitEffects.push_back(zs::EffectDescriptorId{40});
  action.onHitEffects.push_back(zs::EffectDescriptorId{41});
  action.onHitEffects.push_back(zs::EffectDescriptorId{42});

  auto result = pipeline.resolve(action);
  assert(result.appliedEffects.size() == 3);
  assert(effectSys.effect_count(zs::GameplayEntityId{2}) == 3);

  fprintf(stderr, "[combat] test_multiple_on_hit_effects PASS\n");
}

/// ---- Test 40: Stage ordering across different stage types ----
static void test_cross_stage_ordering() {
  fprintf(stderr, "[combat] test_cross_stage_ordering...\n");

  zs::CombatPipeline pipeline{};
  std::vector<std::string> order{};

  pipeline.register_stage(zs::CombatStage::post_calculation,
                          zs::SmallString{"post"},
                          [&order](zs::CombatContext &) -> bool {
                            order.push_back("post");
                            return true;
                          });
  pipeline.register_stage(zs::CombatStage::pre_calculation,
                          zs::SmallString{"pre"},
                          [&order](zs::CombatContext &) -> bool {
                            order.push_back("pre");
                            return true;
                          });
  pipeline.register_stage(zs::CombatStage::damage_calculation,
                          zs::SmallString{"dmg"},
                          [&order](zs::CombatContext &) -> bool {
                            order.push_back("dmg");
                            return true;
                          });
  pipeline.register_stage(zs::CombatStage::mitigation,
                          zs::SmallString{"mit"},
                          [&order](zs::CombatContext &) -> bool {
                            order.push_back("mit");
                            return true;
                          });

  zs::CombatAction action{};
  pipeline.resolve(action);

  assert(order.size() == 4);
  assert(order[0] == "pre");
  assert(order[1] == "dmg");
  assert(order[2] == "mit");
  assert(order[3] == "post");

  fprintf(stderr, "[combat] test_cross_stage_ordering PASS\n");
}

int main() {
  fprintf(stderr, "======================================\n");
  fprintf(stderr, "M6: Combat Resolution Pipeline Tests\n");
  fprintf(stderr, "======================================\n\n");

  test_enums();                           // 1
  test_action_construction();             // 2
  test_result_net_value();                // 3
  test_empty_pipeline();                  // 4
  test_default_pipeline_basic_hit();      // 5
  test_default_pipeline_miss();           // 6
  test_default_pipeline_critical();       // 7
  test_default_pipeline_blocked();        // 8
  test_physical_mitigation();             // 9
  test_magical_mitigation();              // 10
  test_pure_damage();                     // 11
  test_healing_resolution();              // 12
  test_healing_with_stat_scaling();       // 13
  test_damage_with_attack_power();        // 14
  test_combat_events();                   // 15
  test_healing_events();                  // 16
  test_miss_event();                      // 17
  test_critical_event();                  // 18
  test_on_hit_effect_application();       // 19
  test_on_hit_effect_no_apply_on_miss();  // 20
  test_custom_stage_handler();            // 21
  test_stage_priority_ordering();         // 22
  test_pipeline_abort();                  // 23
  test_replace_stage();                   // 24
  test_remove_stage_by_name();            // 25
  test_remove_stage_type();               // 26
  test_batch_resolution();                // 27
  test_health_tracker_basics();           // 28
  test_health_tracker_apply_result();     // 29
  test_health_tracker_set_hp();           // 30
  test_two_entity_combat_scenario();      // 31
  test_effect_application_with_events();  // 32
  test_multiple_handlers_per_stage();     // 33
  test_clear_stages();                    // 34
  test_health_tracker_clear();            // 35
  test_combat_to_death();                 // 36
  test_combined_stats_and_crit();         // 37
  test_result_source_data();              // 38
  test_multiple_on_hit_effects();         // 39
  test_cross_stage_ordering();            // 40

  fprintf(stderr, "\n======================================\n");
  fprintf(stderr, "All 40 combat tests PASSED\n");
  fprintf(stderr, "======================================\n");

  return 0;
}
