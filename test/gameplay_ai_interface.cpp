/// @file gameplay_ai_interface.cpp
/// @brief M9 unit tests — AI-facing interface (GameplayAIInterface.hpp)
///
/// Tests cover:
///   1. AIEntitySnapshot generation and queries
///   2. AIEffectInfo population from EffectSystem
///   3. AIAbilityInfo population from AbilitySystem
///   4. AIActionCandidate feasibility assessment
///   5. Action enumeration with scoring
///   6. best_action selection
///   7. execute_action through AbilitySystem
///   8. decide_and_record debug recording
///   9. Debug JSON export
///  10. Entity registration/unregistration
///  11. Integration with ability, effect, combat, and health subsystems
///  12. Edge cases: no subsystems attached, empty world, dead entities

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "zensim/gameplay/GameplayAIInterface.hpp"
#include "zensim/gameplay/GameplayProgression.hpp"  // for completeness, not strictly needed

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static const double kEps = 1e-6;

static bool near(double a, double b) {
  return std::fabs(a - b) < kEps;
}

/// Build a simple ability descriptor.
static zs::AbilityDescriptor make_ability(
    zs::u64 id, const char *name, const char *category,
    zs::AbilityTargetMode targetMode, double power = 10.0,
    double cooldownTime = 1.0, double range = 5.0) {
  zs::AbilityDescriptor desc;
  desc.id = zs::AbilityDescriptorId{id};
  desc.name = zs::SmallString{name};
  desc.category = zs::SmallString{category};
  desc.targetMode = targetMode;
  desc.power = power;
  desc.cooldownTime = cooldownTime;
  desc.range = range;
  desc.maxCharges = 1;
  desc.chargeRechargeTime = cooldownTime;  // recharge when cooldown finishes
  return desc;
}

/// Build a simple duration effect descriptor with a stat modifier.
static zs::EffectDescriptor make_buff(
    zs::u64 id, const char *name, const char *statName,
    zs::StatModOp op, double value, double duration = 5.0) {
  zs::EffectDescriptor desc;
  desc.id = zs::EffectDescriptorId{id};
  desc.name = zs::SmallString{name};
  desc.durationType = zs::EffectDurationType::duration;
  desc.duration = duration;
  desc.stackPolicy = zs::EffectStackPolicy::replace;
  desc.maxStacks = 1;

  zs::StatModifier mod;
  mod.statName = zs::SmallString{statName};
  mod.operation = op;
  mod.value = value;
  desc.modifiers.push_back(mod);

  return desc;
}

// Entity IDs
static const zs::GameplayEntityId kPlayer{1};
static const zs::GameplayEntityId kEnemy{2};
static const zs::GameplayEntityId kAlly{3};

// Ability descriptor IDs
static const zs::AbilityDescriptorId kFireball{100};
static const zs::AbilityDescriptorId kHeal{101};
static const zs::AbilityDescriptorId kBuff{102};

// Effect descriptor IDs
static const zs::EffectDescriptorId kStrengthBuff{200};
static const zs::EffectDescriptorId kPoison{201};

// ---------------------------------------------------------------------------
//  Test 1: AIWorldView default state
// ---------------------------------------------------------------------------
static void test_default_state() {
  zs::AIWorldView world;
  assert(world.ability_system() == nullptr);
  assert(world.effect_system() == nullptr);
  assert(world.combat_pipeline() == nullptr);
  assert(world.health_tracker() == nullptr);
  assert(world.entity_count() == 0);
  assert(world.debug_history().empty());
  fprintf(stderr, "[PASS] test_default_state\n");
}

// ---------------------------------------------------------------------------
//  Test 2: Entity registration and unregistration
// ---------------------------------------------------------------------------
static void test_entity_registration() {
  zs::AIWorldView world;
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);
  assert(world.entity_count() == 2);

  // Duplicate registration is a no-op
  world.register_entity(kPlayer);
  assert(world.entity_count() == 2);

  world.unregister_entity(kEnemy);
  assert(world.entity_count() == 1);
  assert(world.entities()[0] == kPlayer);

  // Unregister nonexistent is a no-op
  world.unregister_entity(kEnemy);
  assert(world.entity_count() == 1);

  world.clear_entities();
  assert(world.entity_count() == 0);

  fprintf(stderr, "[PASS] test_entity_registration\n");
}

// ---------------------------------------------------------------------------
//  Test 3: Subsystem attachment
// ---------------------------------------------------------------------------
static void test_subsystem_attachment() {
  zs::AbilitySystem abSys;
  zs::EffectSystem efSys;
  zs::CombatPipeline pipeline = zs::CombatPipeline::with_defaults();
  zs::HealthTracker tracker;
  zs::GameplayEventDispatcher dispatcher;

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.set_effect_system(&efSys);
  world.set_combat_pipeline(&pipeline);
  world.set_health_tracker(&tracker);
  world.set_dispatcher(&dispatcher);

  assert(world.ability_system() == &abSys);
  assert(world.effect_system() == &efSys);
  assert(world.combat_pipeline() == &pipeline);
  assert(world.health_tracker() == &tracker);

  fprintf(stderr, "[PASS] test_subsystem_attachment\n");
}

// ---------------------------------------------------------------------------
//  Test 4: Snapshot with no subsystems attached
// ---------------------------------------------------------------------------
static void test_snapshot_no_subsystems() {
  zs::AIWorldView world;
  auto snap = world.snapshot(kPlayer);
  assert(snap.entityId == kPlayer);
  assert(near(snap.currentHp, 0.0));
  assert(near(snap.maxHp, 0.0));
  assert(near(snap.hpFraction, 0.0));
  assert(snap.alive == true);  // default for untracked entity
  assert(snap.abilities.empty());
  assert(snap.activeEffects.empty());
  assert(snap.stats.empty());

  fprintf(stderr, "[PASS] test_snapshot_no_subsystems\n");
}

// ---------------------------------------------------------------------------
//  Test 5: Snapshot with health tracker
// ---------------------------------------------------------------------------
static void test_snapshot_health() {
  zs::HealthTracker tracker;
  tracker.set_max_hp(kPlayer, 100.0);
  tracker.apply_damage(kPlayer, 30.0);

  zs::AIWorldView world;
  world.set_health_tracker(&tracker);

  auto snap = world.snapshot(kPlayer);
  assert(near(snap.currentHp, 70.0));
  assert(near(snap.maxHp, 100.0));
  assert(near(snap.hpFraction, 0.7));
  assert(snap.alive);

  // Dead entity
  tracker.apply_damage(kPlayer, 70.0);
  snap = world.snapshot(kPlayer);
  assert(near(snap.currentHp, 0.0));
  assert(!snap.alive);
  assert(near(snap.hpFraction, 0.0));

  fprintf(stderr, "[PASS] test_snapshot_health\n");
}

// ---------------------------------------------------------------------------
//  Test 6: Snapshot with abilities
// ---------------------------------------------------------------------------
static void test_snapshot_abilities() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0, 2.0, 10.0);
  auto heal = make_ability(kHeal.value, "Heal", "support",
                           zs::AbilityTargetMode::self_only, 15.0, 3.0, 0.0);
  abSys.register_descriptor(fireball);
  abSys.register_descriptor(heal);
  abSys.grant_ability(kPlayer, kFireball);
  abSys.grant_ability(kPlayer, kHeal);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);

  auto snap = world.snapshot(kPlayer);
  assert(snap.abilities.size() == 2);

  // Find fireball
  auto *fbInfo = snap.find_ability(kFireball);
  assert(fbInfo != nullptr);
  assert(std::strcmp(fbInfo->name.asChars(), "Fireball") == 0);
  assert(fbInfo->targetMode == zs::AbilityTargetMode::single_target);
  assert(near(fbInfo->power, 25.0));
  assert(near(fbInfo->range, 10.0));
  assert(fbInfo->canActivate);

  // Find heal
  auto *healInfo = snap.find_ability(kHeal);
  assert(healInfo != nullptr);
  assert(std::strcmp(healInfo->name.asChars(), "Heal") == 0);

  fprintf(stderr, "[PASS] test_snapshot_abilities\n");
}

// ---------------------------------------------------------------------------
//  Test 7: Snapshot with effects and stats
// ---------------------------------------------------------------------------
static void test_snapshot_effects_and_stats() {
  zs::EffectSystem efSys;
  auto strBuff = make_buff(kStrengthBuff.value, "StrengthBuff", "strength",
                           zs::StatModOp::additive, 10.0, 30.0);
  efSys.register_descriptor(strBuff);

  // Set up base stats
  auto &stats = efSys.stat_block(kPlayer);
  stats.set_base(zs::SmallString{"strength"}, 50.0);
  stats.set_base(zs::SmallString{"defense"}, 20.0);

  // Apply the buff
  efSys.apply_effect(kStrengthBuff, kPlayer);

  zs::AIWorldView world;
  world.set_effect_system(&efSys);

  auto snap = world.snapshot(kPlayer);

  // Check effects
  assert(snap.activeEffects.size() == 1);
  assert(snap.activeEffects[0].descriptorId == kStrengthBuff);
  assert(std::strcmp(snap.activeEffects[0].name.asChars(), "StrengthBuff") == 0);
  assert(snap.activeEffects[0].durationType == zs::EffectDurationType::duration);
  assert(snap.activeEffects[0].stackCount == 1);

  // Check stats — strength should be 50 + 10 = 60
  assert(snap.stats.size() == 2);
  assert(near(snap.stat(zs::SmallString{"strength"}), 60.0));
  assert(near(snap.stat(zs::SmallString{"defense"}), 20.0));

  // Non-existent stat returns 0
  assert(near(snap.stat(zs::SmallString{"agility"}), 0.0));

  // has_effect
  assert(snap.has_effect(kStrengthBuff));
  assert(!snap.has_effect(kPoison));

  fprintf(stderr, "[PASS] test_snapshot_effects_and_stats\n");
}

// ---------------------------------------------------------------------------
//  Test 8: has_activatable_ability
// ---------------------------------------------------------------------------
static void test_has_activatable_ability() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target);
  abSys.register_descriptor(fireball);
  auto instId = abSys.grant_ability(kPlayer, kFireball);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);

  auto snap = world.snapshot(kPlayer);
  assert(snap.has_activatable_ability());

  // Activate the ability (need target for single_target mode, puts it on cooldown)
  abSys.try_activate(kPlayer, instId, kEnemy);
  // Tick past cast time (0) and into cooldown
  abSys.tick(0.01);

  snap = world.snapshot(kPlayer);
  // Now on cooldown, should not be activatable
  assert(!snap.has_activatable_ability());

  fprintf(stderr, "[PASS] test_has_activatable_ability\n");
}

// ---------------------------------------------------------------------------
//  Test 9: AIActionKind and AIActionFeasibility name functions
// ---------------------------------------------------------------------------
static void test_enum_name_functions() {
  assert(std::strcmp(zs::ai_action_kind_name(zs::AIActionKind::use_ability), "use_ability") == 0);
  assert(std::strcmp(zs::ai_action_kind_name(zs::AIActionKind::wait), "wait") == 0);
  assert(std::strcmp(zs::ai_action_kind_name(zs::AIActionKind::flee), "flee") == 0);
  assert(std::strcmp(zs::ai_action_kind_name(zs::AIActionKind::move_to), "move_to") == 0);
  assert(std::strcmp(zs::ai_action_kind_name(zs::AIActionKind::use_item), "use_item") == 0);

  assert(std::strcmp(zs::ai_action_feasibility_name(zs::AIActionFeasibility::feasible), "feasible") == 0);
  assert(std::strcmp(zs::ai_action_feasibility_name(zs::AIActionFeasibility::on_cooldown), "on_cooldown") == 0);
  assert(std::strcmp(zs::ai_action_feasibility_name(zs::AIActionFeasibility::no_charges), "no_charges") == 0);
  assert(std::strcmp(zs::ai_action_feasibility_name(zs::AIActionFeasibility::blocked), "blocked") == 0);
  assert(std::strcmp(zs::ai_action_feasibility_name(zs::AIActionFeasibility::not_available), "not_available") == 0);

  fprintf(stderr, "[PASS] test_enum_name_functions\n");
}

// ---------------------------------------------------------------------------
//  Test 10: AIActionCandidate is_feasible
// ---------------------------------------------------------------------------
static void test_action_candidate_feasible() {
  zs::AIActionCandidate c;
  c.feasibility = zs::AIActionFeasibility::feasible;
  assert(c.is_feasible());

  c.feasibility = zs::AIActionFeasibility::on_cooldown;
  assert(!c.is_feasible());

  c.feasibility = zs::AIActionFeasibility::blocked;
  assert(!c.is_feasible());

  fprintf(stderr, "[PASS] test_action_candidate_feasible\n");
}

// ---------------------------------------------------------------------------
//  Test 11: Enumerate actions — no subsystems (only wait)
// ---------------------------------------------------------------------------
static void test_enumerate_actions_no_subsystems() {
  zs::AIWorldView world;
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  auto candidates = world.enumerate_actions(kPlayer);
  // Should have at least the "wait" action
  assert(!candidates.empty());
  assert(candidates[0].kind == zs::AIActionKind::wait);
  assert(candidates[0].is_feasible());

  fprintf(stderr, "[PASS] test_enumerate_actions_no_subsystems\n");
}

// ---------------------------------------------------------------------------
//  Test 12: Enumerate actions — with abilities
// ---------------------------------------------------------------------------
static void test_enumerate_actions_with_abilities() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0);
  auto heal = make_ability(kHeal.value, "Heal", "support",
                           zs::AbilityTargetMode::self_only, 15.0);
  abSys.register_descriptor(fireball);
  abSys.register_descriptor(heal);
  abSys.grant_ability(kPlayer, kFireball);
  abSys.grant_ability(kPlayer, kHeal);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);
  world.register_entity(kAlly);

  auto candidates = world.enumerate_actions(kPlayer);

  // Expected: 1 wait + 1 heal(self) + 2 fireball(enemy, ally) = 4
  // Fireball is single_target so it creates one candidate per non-self entity
  // Heal is self_only so it creates one candidate targeting self
  assert(candidates.size() == 4);

  // All should be feasible since abilities are ready
  size_t feasibleCount = 0;
  for (auto &c : candidates) {
    if (c.is_feasible()) ++feasibleCount;
  }
  assert(feasibleCount == 4);

  fprintf(stderr, "[PASS] test_enumerate_actions_with_abilities\n");
}

// ---------------------------------------------------------------------------
//  Test 13: Enumerate actions — scoring function
// ---------------------------------------------------------------------------
static void test_enumerate_actions_with_scoring() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0);
  auto heal = make_ability(kHeal.value, "Heal", "support",
                           zs::AbilityTargetMode::self_only, 15.0);
  abSys.register_descriptor(fireball);
  abSys.register_descriptor(heal);
  abSys.grant_ability(kPlayer, kFireball);
  abSys.grant_ability(kPlayer, kHeal);

  zs::HealthTracker tracker;
  tracker.set_max_hp(kPlayer, 100.0);
  tracker.apply_damage(kPlayer, 60.0);  // 40 HP left
  tracker.set_max_hp(kEnemy, 100.0);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.set_health_tracker(&tracker);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  // Score function: prioritize healing when HP is low
  world.set_score_function([](const zs::AIEntitySnapshot &actor,
                              const zs::AIEntitySnapshot & /*target*/,
                              const zs::AIActionCandidate &candidate) -> double {
    if (candidate.kind == zs::AIActionKind::wait) return -1.0;
    if (candidate.abilityId == zs::AbilityDescriptorId{101}) {  // Heal
      // Higher score when HP is low
      return (1.0 - actor.hpFraction) * 100.0;  // 0.6 * 100 = 60
    }
    if (candidate.abilityId == zs::AbilityDescriptorId{100}) {  // Fireball
      return 30.0;  // Fixed score
    }
    return 0.0;
  });

  auto candidates = world.enumerate_actions(kPlayer);
  // Candidates are sorted by score descending
  assert(!candidates.empty());
  // Best should be heal (score 60) since HP is low
  assert(candidates[0].abilityId == kHeal);
  assert(candidates[0].score > 50.0);

  fprintf(stderr, "[PASS] test_enumerate_actions_with_scoring\n");
}

// ---------------------------------------------------------------------------
//  Test 14: best_action selects highest-scored feasible action
// ---------------------------------------------------------------------------
static void test_best_action() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0);
  abSys.register_descriptor(fireball);
  abSys.grant_ability(kPlayer, kFireball);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  // Score: fireball = 50, wait = 0
  world.set_score_function([](const zs::AIEntitySnapshot &,
                              const zs::AIEntitySnapshot &,
                              const zs::AIActionCandidate &c) -> double {
    if (c.kind == zs::AIActionKind::use_ability) return 50.0;
    return 0.0;
  });

  auto best = world.best_action(kPlayer);
  assert(best.kind == zs::AIActionKind::use_ability);
  assert(best.abilityId == kFireball);
  assert(best.is_feasible());

  fprintf(stderr, "[PASS] test_best_action\n");
}

// ---------------------------------------------------------------------------
//  Test 15: best_action returns wait when no abilities
// ---------------------------------------------------------------------------
static void test_best_action_no_abilities() {
  zs::AIWorldView world;
  world.register_entity(kPlayer);

  auto best = world.best_action(kPlayer);
  assert(best.kind == zs::AIActionKind::wait);
  assert(best.is_feasible());

  fprintf(stderr, "[PASS] test_best_action_no_abilities\n");
}

// ---------------------------------------------------------------------------
//  Test 16: Feasibility — on cooldown
// ---------------------------------------------------------------------------
static void test_feasibility_on_cooldown() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0, 5.0);
  abSys.register_descriptor(fireball);
  auto instId = abSys.grant_ability(kPlayer, kFireball);

  // Activate and tick into cooldown
  abSys.try_activate(kPlayer, instId, kEnemy);
  abSys.tick(0.01);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  auto candidates = world.enumerate_actions(kPlayer);
  // Find the fireball candidate
  bool foundOnCooldown = false;
  for (auto &c : candidates) {
    if (c.kind == zs::AIActionKind::use_ability && c.abilityId == kFireball) {
      assert(c.feasibility == zs::AIActionFeasibility::on_cooldown);
      foundOnCooldown = true;
    }
  }
  assert(foundOnCooldown);

  fprintf(stderr, "[PASS] test_feasibility_on_cooldown\n");
}

// ---------------------------------------------------------------------------
//  Test 17: Feasibility — blocked ability
// ---------------------------------------------------------------------------
static void test_feasibility_blocked() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target);
  abSys.register_descriptor(fireball);
  auto instId = abSys.grant_ability(kPlayer, kFireball);

  // Block the ability
  abSys.block_ability(kPlayer, instId);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  auto candidates = world.enumerate_actions(kPlayer);
  bool foundBlocked = false;
  for (auto &c : candidates) {
    if (c.kind == zs::AIActionKind::use_ability && c.abilityId == kFireball) {
      assert(c.feasibility == zs::AIActionFeasibility::blocked);
      foundBlocked = true;
    }
  }
  assert(foundBlocked);

  fprintf(stderr, "[PASS] test_feasibility_blocked\n");
}

// ---------------------------------------------------------------------------
//  Test 18: execute_action — use_ability success
// ---------------------------------------------------------------------------
static void test_execute_ability_success() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target);
  abSys.register_descriptor(fireball);
  abSys.grant_ability(kPlayer, kFireball);

  zs::GameplayEventDispatcher dispatcher;

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.set_dispatcher(&dispatcher);

  zs::AIActionRequest request;
  request.actorId = kPlayer;
  request.kind = zs::AIActionKind::use_ability;
  request.abilityId = kFireball;
  request.targetId = kEnemy;

  auto result = world.execute_action(request);
  assert(result.success);
  assert(result.activationResult == zs::AbilityActivationResult::success);

  fprintf(stderr, "[PASS] test_execute_ability_success\n");
}

// ---------------------------------------------------------------------------
//  Test 19: execute_action — ability not found
// ---------------------------------------------------------------------------
static void test_execute_ability_not_found() {
  zs::AbilitySystem abSys;

  zs::AIWorldView world;
  world.set_ability_system(&abSys);

  zs::AIActionRequest request;
  request.actorId = kPlayer;
  request.kind = zs::AIActionKind::use_ability;
  request.abilityId = kFireball;
  request.targetId = kEnemy;

  auto result = world.execute_action(request);
  assert(!result.success);
  assert(std::strcmp(result.reason.asChars(), "ability_not_found") == 0);

  fprintf(stderr, "[PASS] test_execute_ability_not_found\n");
}

// ---------------------------------------------------------------------------
//  Test 20: execute_action — no ability system
// ---------------------------------------------------------------------------
static void test_execute_no_ability_system() {
  zs::AIWorldView world;

  zs::AIActionRequest request;
  request.actorId = kPlayer;
  request.kind = zs::AIActionKind::use_ability;
  request.abilityId = kFireball;

  auto result = world.execute_action(request);
  assert(!result.success);
  assert(std::strcmp(result.reason.asChars(), "no_ability_system") == 0);

  fprintf(stderr, "[PASS] test_execute_no_ability_system\n");
}

// ---------------------------------------------------------------------------
//  Test 21: execute_action — wait always succeeds
// ---------------------------------------------------------------------------
static void test_execute_wait() {
  zs::AIWorldView world;

  zs::AIActionRequest request;
  request.actorId = kPlayer;
  request.kind = zs::AIActionKind::wait;

  auto result = world.execute_action(request);
  assert(result.success);

  fprintf(stderr, "[PASS] test_execute_wait\n");
}

// ---------------------------------------------------------------------------
//  Test 22: execute_action — unsupported action kind
// ---------------------------------------------------------------------------
static void test_execute_unsupported() {
  zs::AIWorldView world;

  zs::AIActionRequest request;
  request.actorId = kPlayer;
  request.kind = zs::AIActionKind::flee;

  auto result = world.execute_action(request);
  assert(!result.success);
  assert(std::strcmp(result.reason.asChars(), "unsupported_action") == 0);

  fprintf(stderr, "[PASS] test_execute_unsupported\n");
}

// ---------------------------------------------------------------------------
//  Test 23: Debug recording — record_decision
// ---------------------------------------------------------------------------
static void test_record_decision() {
  zs::AIWorldView world;

  zs::AIDebugRecord record;
  record.tick = 42;
  record.actorId = kPlayer;
  record.chosenAction.kind = zs::AIActionKind::wait;
  record.chosenAction.label = zs::SmallString{"Wait"};
  record.reasoning = zs::SmallString{"nothing_to_do"};

  world.record_decision(record);
  assert(world.debug_history().size() == 1);
  assert(world.debug_history()[0].tick == 42);
  assert(world.debug_history()[0].actorId == kPlayer);

  fprintf(stderr, "[PASS] test_record_decision\n");
}

// ---------------------------------------------------------------------------
//  Test 24: Debug capacity limit
// ---------------------------------------------------------------------------
static void test_debug_capacity() {
  zs::AIWorldView world;
  world.set_debug_capacity(3);

  for (zs::u64 i = 0; i < 5; ++i) {
    zs::AIDebugRecord record;
    record.tick = i;
    record.actorId = kPlayer;
    world.record_decision(record);
  }

  // Should only have the last 3
  assert(world.debug_history().size() == 3);
  assert(world.debug_history()[0].tick == 2);
  assert(world.debug_history()[1].tick == 3);
  assert(world.debug_history()[2].tick == 4);

  fprintf(stderr, "[PASS] test_debug_capacity\n");
}

// ---------------------------------------------------------------------------
//  Test 25: clear_debug_history
// ---------------------------------------------------------------------------
static void test_clear_debug_history() {
  zs::AIWorldView world;

  zs::AIDebugRecord record;
  record.tick = 1;
  record.actorId = kPlayer;
  world.record_decision(record);

  assert(world.debug_history().size() == 1);
  world.clear_debug_history();
  assert(world.debug_history().empty());

  fprintf(stderr, "[PASS] test_clear_debug_history\n");
}

// ---------------------------------------------------------------------------
//  Test 26: decide_and_record — full cycle
// ---------------------------------------------------------------------------
static void test_decide_and_record() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0);
  abSys.register_descriptor(fireball);
  abSys.grant_ability(kPlayer, kFireball);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  // Score: fireball = 50, wait = 0
  world.set_score_function([](const zs::AIEntitySnapshot &,
                              const zs::AIEntitySnapshot &,
                              const zs::AIActionCandidate &c) -> double {
    if (c.kind == zs::AIActionKind::use_ability) return 50.0;
    return 0.0;
  });

  auto chosen = world.decide_and_record(kPlayer, 10, zs::SmallString{"attack_preferred"});

  // Chosen action should be fireball
  assert(chosen.kind == zs::AIActionKind::use_ability);
  assert(chosen.abilityId == kFireball);

  // Debug record stored
  assert(world.debug_history().size() == 1);
  assert(world.debug_history()[0].tick == 10);
  assert(world.debug_history()[0].actorId == kPlayer);
  assert(std::strcmp(world.debug_history()[0].reasoning.asChars(), "attack_preferred") == 0);
  assert(!world.debug_history()[0].candidates.empty());

  fprintf(stderr, "[PASS] test_decide_and_record\n");
}

// ---------------------------------------------------------------------------
//  Test 27: AIDebugRecord to_json
// ---------------------------------------------------------------------------
static void test_debug_record_to_json() {
  zs::AIDebugRecord record;
  record.tick = 5;
  record.actorId = kPlayer;
  record.chosenAction.kind = zs::AIActionKind::use_ability;
  record.chosenAction.score = 42.0;
  record.chosenAction.label = zs::SmallString{"Fireball"};
  record.chosenAction.feasibility = zs::AIActionFeasibility::feasible;
  record.reasoning = zs::SmallString{"best_dps"};

  zs::AIActionCandidate c1;
  c1.kind = zs::AIActionKind::use_ability;
  c1.score = 42.0;
  c1.label = zs::SmallString{"Fireball"};
  c1.feasibility = zs::AIActionFeasibility::feasible;
  record.candidates.push_back(c1);

  zs::AIActionCandidate c2;
  c2.kind = zs::AIActionKind::wait;
  c2.score = 0.0;
  c2.label = zs::SmallString{"Wait"};
  c2.feasibility = zs::AIActionFeasibility::feasible;
  record.candidates.push_back(c2);

  std::string json = record.to_json();
  // Verify it contains expected fragments
  assert(json.find("\"tick\":5") != std::string::npos);
  assert(json.find("\"actor\":1") != std::string::npos);
  assert(json.find("\"kind\":\"use_ability\"") != std::string::npos);
  assert(json.find("\"score\":42") != std::string::npos);
  assert(json.find("\"label\":\"Fireball\"") != std::string::npos);
  assert(json.find("\"reasoning\":\"best_dps\"") != std::string::npos);
  assert(json.find("\"candidates\":[") != std::string::npos);
  assert(json.find("\"kind\":\"wait\"") != std::string::npos);

  fprintf(stderr, "[PASS] test_debug_record_to_json\n");
}

// ---------------------------------------------------------------------------
//  Test 28: export_debug_json — multiple records
// ---------------------------------------------------------------------------
static void test_export_debug_json() {
  zs::AIWorldView world;

  for (zs::u64 i = 0; i < 3; ++i) {
    zs::AIDebugRecord record;
    record.tick = i;
    record.actorId = kPlayer;
    record.chosenAction.kind = zs::AIActionKind::wait;
    record.chosenAction.label = zs::SmallString{"Wait"};
    record.chosenAction.feasibility = zs::AIActionFeasibility::feasible;
    world.record_decision(record);
  }

  std::string json = world.export_debug_json();
  // Should be a JSON array
  assert(json.front() == '[');
  assert(json.back() == ']');
  // Should have 3 records separated by commas
  assert(json.find("\"tick\":0") != std::string::npos);
  assert(json.find("\"tick\":1") != std::string::npos);
  assert(json.find("\"tick\":2") != std::string::npos);

  fprintf(stderr, "[PASS] test_export_debug_json\n");
}

// ---------------------------------------------------------------------------
//  Test 29: export_debug_json — empty history
// ---------------------------------------------------------------------------
static void test_export_debug_json_empty() {
  zs::AIWorldView world;
  std::string json = world.export_debug_json();
  assert(json == "[]");

  fprintf(stderr, "[PASS] test_export_debug_json_empty\n");
}

// ---------------------------------------------------------------------------
//  Test 30: full clear
// ---------------------------------------------------------------------------
static void test_full_clear() {
  zs::AbilitySystem abSys;
  zs::EffectSystem efSys;

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.set_effect_system(&efSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  zs::AIDebugRecord record;
  record.tick = 1;
  record.actorId = kPlayer;
  world.record_decision(record);

  world.clear();

  assert(world.entity_count() == 0);
  assert(world.debug_history().empty());
  assert(world.ability_system() == nullptr);
  assert(world.effect_system() == nullptr);
  assert(world.combat_pipeline() == nullptr);
  assert(world.health_tracker() == nullptr);

  fprintf(stderr, "[PASS] test_full_clear\n");
}

// ---------------------------------------------------------------------------
//  Test 31: Integration — full AI loop (enumerate, decide, execute)
// ---------------------------------------------------------------------------
static void test_full_ai_loop() {
  // Set up subsystems
  zs::AbilitySystem abSys;
  zs::EffectSystem efSys;
  zs::HealthTracker tracker;
  zs::GameplayEventDispatcher dispatcher;

  // Abilities
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0, 2.0, 10.0);
  auto heal = make_ability(kHeal.value, "Heal", "support",
                           zs::AbilityTargetMode::self_only, 15.0, 3.0);
  abSys.register_descriptor(fireball);
  abSys.register_descriptor(heal);
  abSys.grant_ability(kPlayer, kFireball);
  abSys.grant_ability(kPlayer, kHeal);

  // Health
  tracker.set_max_hp(kPlayer, 100.0);
  tracker.set_max_hp(kEnemy, 80.0);

  // Stats
  efSys.stat_block(kPlayer).set_base(zs::SmallString{"attack"}, 30.0);
  efSys.stat_block(kEnemy).set_base(zs::SmallString{"attack"}, 20.0);

  // World view
  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.set_effect_system(&efSys);
  world.set_health_tracker(&tracker);
  world.set_dispatcher(&dispatcher);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  // Score: prefer fireball on enemy
  world.set_score_function([](const zs::AIEntitySnapshot &actor,
                              const zs::AIEntitySnapshot &target,
                              const zs::AIActionCandidate &c) -> double {
    (void)actor;
    if (c.kind == zs::AIActionKind::wait) return -10.0;
    if (c.abilityId == zs::AbilityDescriptorId{100}) {  // Fireball
      return 100.0 - target.hpFraction * 50.0;
    }
    if (c.abilityId == zs::AbilityDescriptorId{101}) {  // Heal
      return (1.0 - actor.hpFraction) * 80.0;
    }
    return 0.0;
  });

  // Decide
  auto chosen = world.decide_and_record(kPlayer, 1, zs::SmallString{"turn_1"});
  assert(chosen.kind == zs::AIActionKind::use_ability);

  // Execute
  zs::AIActionRequest request;
  request.actorId = kPlayer;
  request.kind = chosen.kind;
  request.abilityId = chosen.abilityId;
  request.targetId = chosen.targetEntity;
  auto result = world.execute_action(request);
  assert(result.success);

  // Debug history captured
  assert(world.debug_history().size() == 1);
  assert(world.debug_history()[0].tick == 1);

  fprintf(stderr, "[PASS] test_full_ai_loop\n");
}

// ---------------------------------------------------------------------------
//  Test 32: Multiple decision turns
// ---------------------------------------------------------------------------
static void test_multiple_turns() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target, 25.0, 1.0);
  abSys.register_descriptor(fireball);
  abSys.grant_ability(kPlayer, kFireball);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  world.set_score_function([](const zs::AIEntitySnapshot &,
                              const zs::AIEntitySnapshot &,
                              const zs::AIActionCandidate &c) -> double {
    if (c.kind == zs::AIActionKind::use_ability) return 50.0;
    return 0.0;
  });

  // Turn 1: fireball should be chosen
  auto t1 = world.decide_and_record(kPlayer, 1);
  assert(t1.kind == zs::AIActionKind::use_ability);

  // Execute to put on cooldown
  zs::AIActionRequest req;
  req.actorId = kPlayer;
  req.kind = t1.kind;
  req.abilityId = t1.abilityId;
  req.targetId = t1.targetEntity;
  world.execute_action(req);
  abSys.tick(0.01);

  // Turn 2: fireball is on cooldown, wait should be chosen
  auto t2 = world.decide_and_record(kPlayer, 2);
  assert(t2.kind == zs::AIActionKind::wait);

  assert(world.debug_history().size() == 2);

  // Tick to clear cooldown
  abSys.tick(1.0);

  // Turn 3: fireball should be available again
  auto t3 = world.decide_and_record(kPlayer, 3);
  assert(t3.kind == zs::AIActionKind::use_ability);

  assert(world.debug_history().size() == 3);

  fprintf(stderr, "[PASS] test_multiple_turns\n");
}

// ---------------------------------------------------------------------------
//  Test 33: Snapshot for entity with no data
// ---------------------------------------------------------------------------
static void test_snapshot_entity_with_no_data() {
  zs::AbilitySystem abSys;
  zs::EffectSystem efSys;
  zs::HealthTracker tracker;

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.set_effect_system(&efSys);
  world.set_health_tracker(&tracker);

  // Entity has no abilities, no effects, no health
  auto snap = world.snapshot(kAlly);
  assert(snap.entityId == kAlly);
  assert(snap.abilities.empty());
  assert(snap.activeEffects.empty());
  assert(snap.stats.empty());
  assert(near(snap.currentHp, 0.0));
  assert(near(snap.maxHp, 0.0));

  fprintf(stderr, "[PASS] test_snapshot_entity_with_no_data\n");
}

// ---------------------------------------------------------------------------
//  Test 34: No-target ability enumeration
// ---------------------------------------------------------------------------
static void test_no_target_ability_enumeration() {
  zs::AbilitySystem abSys;
  auto buff = make_ability(kBuff.value, "Shield", "defensive",
                           zs::AbilityTargetMode::no_target, 0.0, 5.0);
  abSys.register_descriptor(buff);
  abSys.grant_ability(kPlayer, kBuff);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  auto candidates = world.enumerate_actions(kPlayer);
  // Expected: 1 wait + 1 shield(no-target, targets self) = 2
  assert(candidates.size() == 2);

  bool foundShield = false;
  for (auto &c : candidates) {
    if (c.kind == zs::AIActionKind::use_ability && c.abilityId == kBuff) {
      foundShield = true;
      assert(c.targetEntity == kPlayer);  // no_target maps to actor
      assert(c.is_feasible());
    }
  }
  assert(foundShield);

  fprintf(stderr, "[PASS] test_no_target_ability_enumeration\n");
}

// ---------------------------------------------------------------------------
//  Test 35: Score function not set — all scores default to 0
// ---------------------------------------------------------------------------
static void test_no_score_function() {
  zs::AbilitySystem abSys;
  auto fireball = make_ability(kFireball.value, "Fireball", "offensive",
                               zs::AbilityTargetMode::single_target);
  abSys.register_descriptor(fireball);
  abSys.grant_ability(kPlayer, kFireball);

  zs::AIWorldView world;
  world.set_ability_system(&abSys);
  world.register_entity(kPlayer);
  world.register_entity(kEnemy);

  auto candidates = world.enumerate_actions(kPlayer);
  for (auto &c : candidates) {
    assert(near(c.score, 0.0));
  }

  fprintf(stderr, "[PASS] test_no_score_function\n");
}

// ---------------------------------------------------------------------------
//  main
// ---------------------------------------------------------------------------
int main() {
  fprintf(stderr, "=== M9: AI Interface Tests ===\n");

  test_default_state();               // 1
  test_entity_registration();         // 2
  test_subsystem_attachment();        // 3
  test_snapshot_no_subsystems();      // 4
  test_snapshot_health();             // 5
  test_snapshot_abilities();          // 6
  test_snapshot_effects_and_stats();  // 7
  test_has_activatable_ability();     // 8
  test_enum_name_functions();         // 9
  test_action_candidate_feasible();   // 10
  test_enumerate_actions_no_subsystems();    // 11
  test_enumerate_actions_with_abilities();   // 12
  test_enumerate_actions_with_scoring();     // 13
  test_best_action();                 // 14
  test_best_action_no_abilities();    // 15
  test_feasibility_on_cooldown();     // 16
  test_feasibility_blocked();         // 17
  test_execute_ability_success();     // 18
  test_execute_ability_not_found();   // 19
  test_execute_no_ability_system();   // 20
  test_execute_wait();                // 21
  test_execute_unsupported();         // 22
  test_record_decision();             // 23
  test_debug_capacity();              // 24
  test_clear_debug_history();         // 25
  test_decide_and_record();           // 26
  test_debug_record_to_json();        // 27
  test_export_debug_json();           // 28
  test_export_debug_json_empty();     // 29
  test_full_clear();                  // 30
  test_full_ai_loop();               // 31
  test_multiple_turns();             // 32
  test_snapshot_entity_with_no_data(); // 33
  test_no_target_ability_enumeration(); // 34
  test_no_score_function();           // 35

  fprintf(stderr, "=== All 35 M9 AI Interface Tests PASSED ===\n");
  return 0;
}
