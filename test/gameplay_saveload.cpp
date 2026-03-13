/// @file gameplay_saveload.cpp
/// @brief M10 unit tests — Save/Load, Replay, Deterministic, Comparison
///
/// Tests cover:
///   1.  SaveSchemaVersion equality and compatibility
///   2.  GameplayStateSnapshot clear / default state
///   3.  GameplayStateCapture — entity registration
///   4.  GameplayStateCapture — health capture
///   5.  GameplayStateCapture — stat block capture
///   6.  GameplayStateCapture — effect capture
///   7.  GameplayStateCapture — ability capture
///   8.  GameplayStateCapture — progression capture
///   9.  GameplayStateCapture — full round-trip with multiple subsystems
///  10.  GameplayStateRestore — version mismatch rejection
///  11.  GameplayStateRestore — health restore
///  12.  GameplayStateRestore — stat block restore
///  13.  GameplayStateRestore — effect restore (via descriptors)
///  14.  GameplayStateRestore — ability restore (via descriptors)
///  15.  GameplayStateRestore — progression restore
///  16.  GameplayStateRestore — RestoreResult counters
///  17.  SaveMigrationRegistry — register and run single migration
///  18.  SaveMigrationRegistry — chain migration v1.0→1.1→1.2
///  19.  SaveMigrationRegistry — migration failure propagation
///  20.  SaveMigrationRegistry — no migration needed (same version)
///  21.  SaveMigrationRegistry — no path found
///  22.  DeterministicMode — enable/disable, seed
///  23.  DeterministicMode — next_random produces values in [0,1)
///  24.  DeterministicMode — deterministic sequence repeatability
///  25.  DeterministicMode — reset re-seeds
///  26.  DeterministicMode — rng_state save/restore
///  27.  InputRecorder — start/stop/is_recording
///  28.  InputRecorder — record and record_ability
///  29.  InputRecorder — not recording ignores records
///  30.  InputRecorder — clear
///  31.  InputReplayer — load and advance_to
///  32.  InputReplayer — finished/remaining/cursor
///  33.  InputReplayer — execute ability through AbilitySystem
///  34.  InputReplayer — load_from InputRecorder
///  35.  StateComparator — identical snapshots
///  36.  StateComparator — health divergence
///  37.  StateComparator — stat divergence
///  38.  StateComparator — effect count divergence
///  39.  StateComparator — progression divergence
///  40.  StateComparator — format_divergences text
///  41.  StateComparator — format_divergences_json
///  42.  snapshot_to_json output

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "zensim/gameplay/GameplaySaveLoad.hpp"

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static const double kEps = 1e-6;

static bool near(double a, double b) {
  return std::fabs(a - b) < kEps;
}

/// Build a simple ability descriptor.
static zs::AbilityDescriptor make_ability(
    zs::u64 id, const char *name, double cooldown = 1.0) {
  zs::AbilityDescriptor desc;
  desc.id = zs::AbilityDescriptorId{id};
  desc.name = zs::SmallString{name};
  desc.category = zs::SmallString{"attack"};
  desc.targetMode = zs::AbilityTargetMode::single_target;
  desc.power = 10.0;
  desc.cooldownTime = cooldown;
  desc.range = 5.0;
  desc.maxCharges = 1;
  desc.chargeRechargeTime = cooldown;
  return desc;
}

/// Build a simple duration effect descriptor.
static zs::EffectDescriptor make_effect(
    zs::u64 id, const char *name, double duration = 5.0) {
  zs::EffectDescriptor desc;
  desc.id = zs::EffectDescriptorId{id};
  desc.name = zs::SmallString{name};
  desc.durationType = zs::EffectDurationType::duration;
  desc.duration = duration;
  desc.period = 0.0;
  desc.stackPolicy = zs::EffectStackPolicy::independent;
  desc.maxStacks = 1;
  return desc;
}

// =====================================================================
//  Test 1: SaveSchemaVersion equality and compatibility
// =====================================================================
static void test_version_compat() {
  fprintf(stderr, "[saveload] test_version_compat...\n");

  zs::SaveSchemaVersion v10{1, 0};
  zs::SaveSchemaVersion v11{1, 1};
  zs::SaveSchemaVersion v12{1, 2};
  zs::SaveSchemaVersion v20{2, 0};

  // Equality
  assert(v10 == v10);
  assert(v10 != v11);
  assert(v10 != v20);

  // Compatible: same major, loaded.minor <= current.minor
  assert(v11.is_compatible(v10));  // current=1.1, loaded=1.0 => ok
  assert(v11.is_compatible(v11));  // same => ok
  assert(!v11.is_compatible(v12)); // current=1.1, loaded=1.2 => no (loaded.minor > current.minor)
  assert(!v11.is_compatible(v20)); // different major => no

  // v1.0 current can load v1.0, but not v1.1
  assert(v10.is_compatible(v10));
  assert(!v10.is_compatible(v11));

  fprintf(stderr, "[saveload] test_version_compat PASS\n");
}

// =====================================================================
//  Test 2: GameplayStateSnapshot clear / default state
// =====================================================================
static void test_snapshot_clear() {
  fprintf(stderr, "[saveload] test_snapshot_clear...\n");

  zs::GameplayStateSnapshot snap;
  snap.label = zs::SmallString{"test"};
  snap.tickCount = 42;
  snap.entityIds.push_back(1);
  snap.health.push_back({1, 100.0, 100.0});

  assert(!snap.entityIds.empty());
  assert(!snap.health.empty());
  assert(snap.tickCount == 42);

  snap.clear();
  assert(snap.entityIds.empty());
  assert(snap.health.empty());
  assert(snap.effects.empty());
  assert(snap.statBlocks.empty());
  assert(snap.abilities.empty());
  assert(snap.progression.empty());
  assert(snap.tickCount == 0);
  assert(snap.timestamp == 0);

  fprintf(stderr, "[saveload] test_snapshot_clear PASS\n");
}

// =====================================================================
//  Test 3: GameplayStateCapture — entity registration
// =====================================================================
static void test_capture_entity_registration() {
  fprintf(stderr, "[saveload] test_capture_entity_registration...\n");

  zs::GameplayStateCapture capture;

  // Register entities
  capture.register_entity(zs::GameplayEntityId{1});
  capture.register_entity(zs::GameplayEntityId{2});
  capture.register_entity(zs::GameplayEntityId{3});

  // Duplicate registration should be ignored
  capture.register_entity(zs::GameplayEntityId{1});

  auto snap = capture.capture();
  assert(snap.entityIds.size() == 3);
  assert(snap.entityIds[0] == 1);
  assert(snap.entityIds[1] == 2);
  assert(snap.entityIds[2] == 3);

  // Unregister
  capture.unregister_entity(zs::GameplayEntityId{2});
  snap = capture.capture();
  assert(snap.entityIds.size() == 2);

  // Clear all
  capture.clear_entities();
  snap = capture.capture();
  assert(snap.entityIds.empty());

  fprintf(stderr, "[saveload] test_capture_entity_registration PASS\n");
}

// =====================================================================
//  Test 4: GameplayStateCapture — health capture
// =====================================================================
static void test_capture_health() {
  fprintf(stderr, "[saveload] test_capture_health...\n");

  zs::HealthTracker health;
  health.set_max_hp(zs::GameplayEntityId{1}, 100.0);
  health.apply_damage(zs::GameplayEntityId{1}, 25.0);

  zs::GameplayStateCapture capture;
  capture.set_health_tracker(&health);
  capture.register_entity(zs::GameplayEntityId{1});

  auto snap = capture.capture(zs::SmallString{"health_test"}, 10);
  assert(snap.label == zs::SmallString{"health_test"});
  assert(snap.tickCount == 10);
  assert(snap.health.size() == 1);
  assert(snap.health[0].entityId == 1);
  assert(near(snap.health[0].maxHp, 100.0));
  assert(near(snap.health[0].currentHp, 75.0));

  fprintf(stderr, "[saveload] test_capture_health PASS\n");
}

// =====================================================================
//  Test 5: GameplayStateCapture — stat block capture
// =====================================================================
static void test_capture_stat_block() {
  fprintf(stderr, "[saveload] test_capture_stat_block...\n");

  zs::EffectSystem effects;
  auto &sb = effects.stat_block(zs::GameplayEntityId{1});
  sb.set_base(zs::SmallString{"strength"}, 20.0);
  sb.set_base(zs::SmallString{"defense"}, 10.0);

  zs::GameplayStateCapture capture;
  capture.set_effect_system(&effects);
  capture.register_entity(zs::GameplayEntityId{1});

  auto snap = capture.capture();
  assert(snap.statBlocks.size() == 1);
  assert(snap.statBlocks[0].entityId == 1);
  assert(snap.statBlocks[0].baseValues.size() == 2);

  // Verify both stat values are present
  bool foundStr = false, foundDef = false;
  for (auto &bv : snap.statBlocks[0].baseValues) {
    if (std::strcmp(bv.first.asChars(), "strength") == 0) {
      assert(near(bv.second, 20.0));
      foundStr = true;
    } else if (std::strcmp(bv.first.asChars(), "defense") == 0) {
      assert(near(bv.second, 10.0));
      foundDef = true;
    }
  }
  assert(foundStr && foundDef);

  fprintf(stderr, "[saveload] test_capture_stat_block PASS\n");
}

// =====================================================================
//  Test 6: GameplayStateCapture — effect capture
// =====================================================================
static void test_capture_effects() {
  fprintf(stderr, "[saveload] test_capture_effects...\n");

  zs::EffectSystem effects;
  auto desc = make_effect(100, "burn", 10.0);
  effects.register_descriptor(desc);

  zs::GameplayEntityId entity{1};
  zs::GameplayEntityId source{2};
  auto instId = effects.apply_effect(desc.id, entity, source);
  assert(instId.valid());

  zs::GameplayStateCapture capture;
  capture.set_effect_system(&effects);
  capture.register_entity(entity);

  auto snap = capture.capture();
  assert(snap.effects.size() == 1);
  assert(snap.effects[0].descriptorId == 100);
  assert(snap.effects[0].targetId == 1);
  assert(snap.effects[0].sourceId == 2);
  assert(snap.effects[0].active == true);
  assert(near(snap.effects[0].durationRemaining, 10.0));

  fprintf(stderr, "[saveload] test_capture_effects PASS\n");
}

// =====================================================================
//  Test 7: GameplayStateCapture — ability capture
// =====================================================================
static void test_capture_abilities() {
  fprintf(stderr, "[saveload] test_capture_abilities...\n");

  zs::AbilitySystem abilities;
  auto desc = make_ability(200, "fireball", 2.0);
  abilities.register_descriptor(desc);

  zs::GameplayEntityId entity{1};
  auto instId = abilities.grant_ability(entity, desc.id);
  assert(instId.valid());

  zs::GameplayStateCapture capture;
  capture.set_ability_system(&abilities);
  capture.register_entity(entity);

  auto snap = capture.capture();
  assert(snap.abilities.size() == 1);
  assert(snap.abilities[0].descriptorId == 200);
  assert(snap.abilities[0].ownerId == 1);
  assert(snap.abilities[0].currentCharges == 1);

  fprintf(stderr, "[saveload] test_capture_abilities PASS\n");
}

// =====================================================================
//  Test 8: GameplayStateCapture — progression capture
// =====================================================================
static void test_capture_progression() {
  fprintf(stderr, "[saveload] test_capture_progression...\n");

  zs::ProgressionSystem progression;
  auto table = zs::LevelThresholdTable::linear(100, 10);
  progression.set_threshold_table(table);

  zs::GameplayEntityId entity{1};
  auto &prof = progression.profile(entity);
  prof.level = 5;
  prof.totalXp = 400;
  prof.skillPoints = 3;
  prof.totalSkillPoints = 5;
  prof.skillRanks[101] = 2;  // node 101, rank 2
  prof.skillRanks[102] = 1;  // node 102, rank 1

  zs::GameplayStateCapture capture;
  capture.set_progression_system(&progression);
  capture.register_entity(entity);

  auto snap = capture.capture();
  assert(snap.progression.size() == 1);
  assert(snap.progression[0].entityId == 1);
  assert(snap.progression[0].level == 5);
  assert(snap.progression[0].totalXp == 400);
  assert(snap.progression[0].skillPoints == 3);
  assert(snap.progression[0].totalSkillPoints == 5);
  assert(snap.progression[0].skillRanks.size() == 2);

  fprintf(stderr, "[saveload] test_capture_progression PASS\n");
}

// =====================================================================
//  Test 9: Full round-trip capture→restore→capture, verify identical
// =====================================================================
static void test_round_trip() {
  fprintf(stderr, "[saveload] test_round_trip...\n");

  // --- Setup subsystems with state ---
  zs::EffectSystem effects;
  auto effDesc = make_effect(100, "regen", 30.0);
  effects.register_descriptor(effDesc);

  zs::AbilitySystem abilities;
  auto ablDesc = make_ability(200, "heal", 3.0);
  abilities.register_descriptor(ablDesc);

  zs::HealthTracker health;
  zs::ProgressionSystem progression;
  auto table = zs::LevelThresholdTable::linear(100, 10);
  progression.set_threshold_table(table);

  zs::GameplayEntityId e1{1};

  // Populate
  health.set_max_hp(e1, 200.0);
  health.apply_damage(e1, 50.0);  // 150 HP

  auto &sb = effects.stat_block(e1);
  sb.set_base(zs::SmallString{"attack"}, 25.0);

  effects.apply_effect(effDesc.id, e1, e1);
  abilities.grant_ability(e1, ablDesc.id);

  auto &prof = progression.profile(e1);
  prof.level = 3;
  prof.totalXp = 200;
  prof.skillPoints = 2;

  // --- Capture ---
  zs::GameplayStateCapture capture;
  capture.set_effect_system(&effects);
  capture.set_ability_system(&abilities);
  capture.set_health_tracker(&health);
  capture.set_progression_system(&progression);
  capture.register_entity(e1);

  auto snap1 = capture.capture(zs::SmallString{"round_trip"}, 100);

  // --- Restore into fresh subsystems ---
  zs::EffectSystem effects2;
  effects2.register_descriptor(effDesc);  // descriptors must be pre-registered

  zs::AbilitySystem abilities2;
  abilities2.register_descriptor(ablDesc);

  zs::HealthTracker health2;
  zs::ProgressionSystem progression2;
  progression2.set_threshold_table(table);

  zs::GameplayStateRestore restorer;
  restorer.set_effect_system(&effects2);
  restorer.set_ability_system(&abilities2);
  restorer.set_health_tracker(&health2);
  restorer.set_progression_system(&progression2);

  auto result = restorer.restore(snap1);
  assert(result.success);
  assert(result.entitiesRestored == 1);
  assert(result.healthRestored == 1);
  assert(result.effectsRestored == 1);
  assert(result.abilitiesRestored == 1);
  assert(result.progressionRestored == 1);

  // --- Capture from restored subsystems ---
  zs::GameplayStateCapture capture2;
  capture2.set_effect_system(&effects2);
  capture2.set_ability_system(&abilities2);
  capture2.set_health_tracker(&health2);
  capture2.set_progression_system(&progression2);
  capture2.register_entity(e1);

  auto snap2 = capture2.capture(zs::SmallString{"round_trip_2"}, 100);

  // --- Compare snapshots ---
  // Health should match
  assert(snap2.health.size() == snap1.health.size());
  assert(near(snap2.health[0].currentHp, snap1.health[0].currentHp));
  assert(near(snap2.health[0].maxHp, snap1.health[0].maxHp));

  // Stat block base values should match
  assert(snap2.statBlocks.size() == snap1.statBlocks.size());

  // Progression should match
  assert(snap2.progression.size() == snap1.progression.size());
  assert(snap2.progression[0].level == snap1.progression[0].level);
  assert(snap2.progression[0].totalXp == snap1.progression[0].totalXp);
  assert(snap2.progression[0].skillPoints == snap1.progression[0].skillPoints);

  // Effects and abilities should match in count
  assert(snap2.effects.size() == snap1.effects.size());
  assert(snap2.abilities.size() == snap1.abilities.size());

  fprintf(stderr, "[saveload] test_round_trip PASS\n");
}

// =====================================================================
//  Test 10: GameplayStateRestore — version mismatch rejection
// =====================================================================
static void test_restore_version_mismatch() {
  fprintf(stderr, "[saveload] test_restore_version_mismatch...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {2, 0};  // major 2

  zs::GameplayStateRestore restorer;
  auto result = restorer.restore(snap, {1, 0});  // expecting 1.0
  assert(!result.success);
  assert(std::strcmp(result.error.asChars(), "version_mismatch") == 0);

  // Minor too new
  snap.version = {1, 3};
  result = restorer.restore(snap, {1, 2});  // expecting 1.2, got 1.3
  assert(!result.success);

  // Compatible: loaded minor <= expected minor
  snap.version = {1, 0};
  result = restorer.restore(snap, {1, 2});  // expecting 1.2, got 1.0 => ok
  assert(result.success);

  fprintf(stderr, "[saveload] test_restore_version_mismatch PASS\n");
}

// =====================================================================
//  Test 11: GameplayStateRestore — health restore
// =====================================================================
static void test_restore_health() {
  fprintf(stderr, "[saveload] test_restore_health...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  zs::SavedHealthEntry he;
  he.entityId = 1;
  he.currentHp = 75.0;
  he.maxHp = 100.0;
  snap.health.push_back(he);
  snap.entityIds.push_back(1);

  zs::HealthTracker health;
  zs::GameplayStateRestore restorer;
  restorer.set_health_tracker(&health);

  auto result = restorer.restore(snap);
  assert(result.success);
  assert(result.healthRestored == 1);

  zs::GameplayEntityId e1{1};
  assert(near(health.max_hp(e1), 100.0));
  assert(near(health.current_hp(e1), 75.0));

  fprintf(stderr, "[saveload] test_restore_health PASS\n");
}

// =====================================================================
//  Test 12: GameplayStateRestore — stat block restore
// =====================================================================
static void test_restore_stat_block() {
  fprintf(stderr, "[saveload] test_restore_stat_block...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  zs::SavedStatBlock ssb;
  ssb.entityId = 1;
  ssb.baseValues.push_back({zs::SmallString{"attack"}, 30.0});
  ssb.baseValues.push_back({zs::SmallString{"speed"}, 15.0});
  snap.statBlocks.push_back(ssb);
  snap.entityIds.push_back(1);

  zs::EffectSystem effects;
  zs::GameplayStateRestore restorer;
  restorer.set_effect_system(&effects);

  auto result = restorer.restore(snap);
  assert(result.success);

  auto *sb = effects.find_stat_block(zs::GameplayEntityId{1});
  assert(sb != nullptr);
  assert(near(sb->base(zs::SmallString{"attack"}), 30.0));
  assert(near(sb->base(zs::SmallString{"speed"}), 15.0));

  fprintf(stderr, "[saveload] test_restore_stat_block PASS\n");
}

// =====================================================================
//  Test 13: GameplayStateRestore — effect restore (via descriptors)
// =====================================================================
static void test_restore_effects() {
  fprintf(stderr, "[saveload] test_restore_effects...\n");

  // Prepare snapshot with one effect
  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  zs::SavedEffectInstance se;
  se.instanceId = 1;
  se.descriptorId = 100;
  se.targetId = 1;
  se.sourceId = 2;
  se.active = true;
  se.durationRemaining = 5.0;
  se.stackCount = 1;
  snap.effects.push_back(se);
  snap.entityIds.push_back(1);

  // Pre-register descriptor
  zs::EffectSystem effects;
  auto desc = make_effect(100, "poison", 10.0);
  effects.register_descriptor(desc);

  zs::GameplayStateRestore restorer;
  restorer.set_effect_system(&effects);

  auto result = restorer.restore(snap);
  assert(result.success);
  assert(result.effectsRestored == 1);

  // Verify the effect was applied
  assert(effects.has_effect(zs::GameplayEntityId{1}, desc.id));

  fprintf(stderr, "[saveload] test_restore_effects PASS\n");
}

// =====================================================================
//  Test 14: GameplayStateRestore — ability restore (via descriptors)
// =====================================================================
static void test_restore_abilities() {
  fprintf(stderr, "[saveload] test_restore_abilities...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  zs::SavedAbilityInstance sa;
  sa.instanceId = 1;
  sa.descriptorId = 200;
  sa.ownerId = 1;
  sa.state = 0;
  sa.currentCharges = 1;
  snap.abilities.push_back(sa);
  snap.entityIds.push_back(1);

  zs::AbilitySystem abilities;
  auto desc = make_ability(200, "slash", 1.0);
  abilities.register_descriptor(desc);

  zs::GameplayStateRestore restorer;
  restorer.set_ability_system(&abilities);

  auto result = restorer.restore(snap);
  assert(result.success);
  assert(result.abilitiesRestored == 1);

  // Verify ability was granted
  auto insts = abilities.entity_abilities(zs::GameplayEntityId{1});
  assert(insts.size() == 1);
  assert(insts[0]->descriptorId.value == 200);

  fprintf(stderr, "[saveload] test_restore_abilities PASS\n");
}

// =====================================================================
//  Test 15: GameplayStateRestore — progression restore
// =====================================================================
static void test_restore_progression() {
  fprintf(stderr, "[saveload] test_restore_progression...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  zs::SavedProgressionProfile spp;
  spp.entityId = 1;
  spp.level = 7;
  spp.totalXp = 600;
  spp.skillPoints = 4;
  spp.totalSkillPoints = 6;
  spp.skillRanks.push_back({101, 2});
  snap.progression.push_back(spp);
  snap.entityIds.push_back(1);

  zs::ProgressionSystem progression;
  auto table = zs::LevelThresholdTable::linear(100, 10);
  progression.set_threshold_table(table);

  zs::GameplayStateRestore restorer;
  restorer.set_progression_system(&progression);

  auto result = restorer.restore(snap);
  assert(result.success);
  assert(result.progressionRestored == 1);

  auto *prof = progression.find_profile(zs::GameplayEntityId{1});
  assert(prof != nullptr);
  assert(prof->level == 7);
  assert(prof->totalXp == 600);
  assert(prof->skillPoints == 4);
  assert(prof->totalSkillPoints == 6);
  assert(prof->skillRanks.size() == 1);
  assert(prof->skill_rank(zs::SkillNodeId{101}) == 2);

  fprintf(stderr, "[saveload] test_restore_progression PASS\n");
}

// =====================================================================
//  Test 16: GameplayStateRestore — RestoreResult counters
// =====================================================================
static void test_restore_counters() {
  fprintf(stderr, "[saveload] test_restore_counters...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};

  // 2 health entries
  snap.health.push_back({1, 100.0, 100.0});
  snap.health.push_back({2, 80.0, 100.0});

  // 1 progression entry
  zs::SavedProgressionProfile spp;
  spp.entityId = 1;
  spp.level = 2;
  snap.progression.push_back(spp);

  snap.entityIds = {1, 2};

  zs::HealthTracker health;
  zs::ProgressionSystem progression;
  auto table = zs::LevelThresholdTable::linear(100, 10);
  progression.set_threshold_table(table);

  zs::GameplayStateRestore restorer;
  restorer.set_health_tracker(&health);
  restorer.set_progression_system(&progression);

  auto result = restorer.restore(snap);
  assert(result.success);
  assert(result.entitiesRestored == 2);
  assert(result.healthRestored == 2);
  assert(result.progressionRestored == 1);
  assert(result.effectsRestored == 0);
  assert(result.abilitiesRestored == 0);

  fprintf(stderr, "[saveload] test_restore_counters PASS\n");
}

// =====================================================================
//  Test 17: SaveMigrationRegistry — register and run single migration
// =====================================================================
static void test_migration_single() {
  fprintf(stderr, "[saveload] test_migration_single...\n");

  zs::SaveMigrationRegistry registry;
  assert(registry.migration_count() == 0);

  registry.register_migration(
    {1, 0}, {1, 1},
    [](zs::GameplayStateSnapshot &snap) -> bool {
      // Migration: add a label
      snap.label = zs::SmallString{"migrated_1_1"};
      return true;
    });

  assert(registry.migration_count() == 1);

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  bool ok = registry.migrate(snap, {1, 1});
  assert(ok);
  zs::SaveSchemaVersion expected_v11{1, 1};
  assert(snap.version == expected_v11);
  assert(std::strcmp(snap.label.asChars(), "migrated_1_1") == 0);

  fprintf(stderr, "[saveload] test_migration_single PASS\n");
}

// =====================================================================
//  Test 18: SaveMigrationRegistry — chain migration v1.0→1.1→1.2
// =====================================================================
static void test_migration_chain() {
  fprintf(stderr, "[saveload] test_migration_chain...\n");

  zs::SaveMigrationRegistry registry;
  registry.register_migration(
    {1, 0}, {1, 1},
    [](zs::GameplayStateSnapshot &snap) -> bool {
      snap.label = zs::SmallString{"step1"};
      return true;
    });
  registry.register_migration(
    {1, 1}, {1, 2},
    [](zs::GameplayStateSnapshot &snap) -> bool {
      snap.tickCount = 999;
      return true;
    });

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  bool ok = registry.migrate(snap, {1, 2});
  assert(ok);
  zs::SaveSchemaVersion expected_v12{1, 2};
  assert(snap.version == expected_v12);
  assert(std::strcmp(snap.label.asChars(), "step1") == 0);
  assert(snap.tickCount == 999);

  fprintf(stderr, "[saveload] test_migration_chain PASS\n");
}

// =====================================================================
//  Test 19: SaveMigrationRegistry — migration failure propagation
// =====================================================================
static void test_migration_failure() {
  fprintf(stderr, "[saveload] test_migration_failure...\n");

  zs::SaveMigrationRegistry registry;
  registry.register_migration(
    {1, 0}, {1, 1},
    [](zs::GameplayStateSnapshot &) -> bool {
      return false;  // Failure
    });
  registry.register_migration(
    {1, 1}, {1, 2},
    [](zs::GameplayStateSnapshot &snap) -> bool {
      snap.tickCount = 42;
      return true;
    });

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  bool ok = registry.migrate(snap, {1, 2});
  assert(!ok);
  // Version should still be 1.0 since migration failed
  zs::SaveSchemaVersion expected_v10{1, 0};
  assert(snap.version == expected_v10);

  fprintf(stderr, "[saveload] test_migration_failure PASS\n");
}

// =====================================================================
//  Test 20: SaveMigrationRegistry — no migration needed (same version)
// =====================================================================
static void test_migration_same_version() {
  fprintf(stderr, "[saveload] test_migration_same_version...\n");

  zs::SaveMigrationRegistry registry;
  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};

  bool ok = registry.migrate(snap, {1, 0});
  assert(ok);
  zs::SaveSchemaVersion expected_v10b{1, 0};
  assert(snap.version == expected_v10b);

  fprintf(stderr, "[saveload] test_migration_same_version PASS\n");
}

// =====================================================================
//  Test 21: SaveMigrationRegistry — no path found
// =====================================================================
static void test_migration_no_path() {
  fprintf(stderr, "[saveload] test_migration_no_path...\n");

  zs::SaveMigrationRegistry registry;
  registry.register_migration(
    {1, 0}, {1, 1},
    [](zs::GameplayStateSnapshot &) -> bool { return true; });

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};

  // No path from 1.0 → 2.0 (only path goes to 1.1)
  bool ok = registry.migrate(snap, {2, 0});
  assert(!ok);

  fprintf(stderr, "[saveload] test_migration_no_path PASS\n");
}

// =====================================================================
//  Test 22: DeterministicMode — enable/disable, seed
// =====================================================================
static void test_deterministic_basic() {
  fprintf(stderr, "[saveload] test_deterministic_basic...\n");

  zs::DeterministicMode det;
  assert(!det.enabled());
  assert(det.seed() == 12345);

  det.set_enabled(true);
  assert(det.enabled());

  det.set_seed(42);
  assert(det.seed() == 42);
  assert(det.rng_state() == 42);

  det.set_enabled(false);
  assert(!det.enabled());

  fprintf(stderr, "[saveload] test_deterministic_basic PASS\n");
}

// =====================================================================
//  Test 23: DeterministicMode — next_random produces values in [0,1)
// =====================================================================
static void test_deterministic_range() {
  fprintf(stderr, "[saveload] test_deterministic_range...\n");

  zs::DeterministicMode det;
  det.set_seed(987654321);

  for (int i = 0; i < 1000; ++i) {
    double val = det.next_random();
    assert(val >= 0.0 && val < 1.0);
  }

  fprintf(stderr, "[saveload] test_deterministic_range PASS\n");
}

// =====================================================================
//  Test 24: DeterministicMode — deterministic sequence repeatability
// =====================================================================
static void test_deterministic_repeatability() {
  fprintf(stderr, "[saveload] test_deterministic_repeatability...\n");

  zs::DeterministicMode det1;
  det1.set_seed(12345);

  zs::DeterministicMode det2;
  det2.set_seed(12345);

  // Same seed should produce same sequence
  for (int i = 0; i < 100; ++i) {
    double v1 = det1.next_random();
    double v2 = det2.next_random();
    assert(v1 == v2);
  }

  // Different seed should produce different sequence
  zs::DeterministicMode det3;
  det3.set_seed(99999);
  double first_same = 0.0;
  {
    zs::DeterministicMode tmp;
    tmp.set_seed(12345);
    first_same = tmp.next_random();
  }
  double first_diff = det3.next_random();
  assert(first_same != first_diff);

  fprintf(stderr, "[saveload] test_deterministic_repeatability PASS\n");
}

// =====================================================================
//  Test 25: DeterministicMode — reset re-seeds
// =====================================================================
static void test_deterministic_reset() {
  fprintf(stderr, "[saveload] test_deterministic_reset...\n");

  zs::DeterministicMode det;
  det.set_seed(42);

  double v1 = det.next_random();
  double v2 = det.next_random();

  // Reset and verify same first value
  det.reset();
  double v1b = det.next_random();
  double v2b = det.next_random();
  assert(v1 == v1b);
  assert(v2 == v2b);

  fprintf(stderr, "[saveload] test_deterministic_reset PASS\n");
}

// =====================================================================
//  Test 26: DeterministicMode — rng_state save/restore
// =====================================================================
static void test_deterministic_state() {
  fprintf(stderr, "[saveload] test_deterministic_state...\n");

  zs::DeterministicMode det;
  det.set_seed(777);

  // Advance a few times
  det.next_random();
  det.next_random();
  det.next_random();

  // Save state
  zs::u64 savedState = det.rng_state();

  // Continue generating
  double v1 = det.next_random();
  double v2 = det.next_random();

  // Restore state and verify same sequence continues
  det.set_rng_state(savedState);
  assert(det.rng_state() == savedState);
  double v1b = det.next_random();
  double v2b = det.next_random();
  assert(v1 == v1b);
  assert(v2 == v2b);

  fprintf(stderr, "[saveload] test_deterministic_state PASS\n");
}

// =====================================================================
//  Test 27: InputRecorder — start/stop/is_recording
// =====================================================================
static void test_recorder_startstop() {
  fprintf(stderr, "[saveload] test_recorder_startstop...\n");

  zs::InputRecorder recorder;
  assert(!recorder.is_recording());
  assert(recorder.input_count() == 0);

  recorder.start(10);
  assert(recorder.is_recording());
  assert(recorder.start_tick() == 10);

  recorder.stop();
  assert(!recorder.is_recording());

  fprintf(stderr, "[saveload] test_recorder_startstop PASS\n");
}

// =====================================================================
//  Test 28: InputRecorder — record and record_ability
// =====================================================================
static void test_recorder_record() {
  fprintf(stderr, "[saveload] test_recorder_record...\n");

  zs::InputRecorder recorder;
  recorder.start(0);

  // Record a generic input
  zs::RecordedInput input;
  input.tick = 1;
  input.actorId = 10;
  input.actionType = zs::SmallString{"move"};
  input.numericParam = 3.14;
  recorder.record(input);

  // Record an ability
  recorder.record_ability(2, 10, 200, 20);

  assert(recorder.input_count() == 2);
  assert(recorder.inputs()[0].tick == 1);
  assert(std::strcmp(recorder.inputs()[0].actionType.asChars(), "move") == 0);
  assert(recorder.inputs()[1].tick == 2);
  assert(std::strcmp(recorder.inputs()[1].actionType.asChars(), "ability") == 0);
  assert(recorder.inputs()[1].abilityId == 200);
  assert(recorder.inputs()[1].targetId == 20);

  fprintf(stderr, "[saveload] test_recorder_record PASS\n");
}

// =====================================================================
//  Test 29: InputRecorder — not recording ignores records
// =====================================================================
static void test_recorder_not_recording() {
  fprintf(stderr, "[saveload] test_recorder_not_recording...\n");

  zs::InputRecorder recorder;
  // Not started
  zs::RecordedInput input;
  input.tick = 1;
  recorder.record(input);
  assert(recorder.input_count() == 0);

  recorder.record_ability(1, 10, 200);
  assert(recorder.input_count() == 0);

  // Start, record, stop, then record more — should be ignored after stop
  recorder.start(0);
  recorder.record_ability(1, 10, 200);
  assert(recorder.input_count() == 1);
  recorder.stop();
  recorder.record_ability(2, 10, 201);
  assert(recorder.input_count() == 1);

  fprintf(stderr, "[saveload] test_recorder_not_recording PASS\n");
}

// =====================================================================
//  Test 30: InputRecorder — clear
// =====================================================================
static void test_recorder_clear() {
  fprintf(stderr, "[saveload] test_recorder_clear...\n");

  zs::InputRecorder recorder;
  recorder.start(5);
  recorder.record_ability(6, 1, 100);
  recorder.record_ability(7, 1, 101);
  assert(recorder.input_count() == 2);

  recorder.clear();
  assert(recorder.input_count() == 0);
  assert(!recorder.is_recording());
  assert(recorder.start_tick() == 0);

  fprintf(stderr, "[saveload] test_recorder_clear PASS\n");
}

// =====================================================================
//  Test 31: InputReplayer — load and advance_to
// =====================================================================
static void test_replayer_advance() {
  fprintf(stderr, "[saveload] test_replayer_advance...\n");

  std::vector<zs::RecordedInput> inputs;
  {
    zs::RecordedInput r;
    r.tick = 1; r.actorId = 1; r.actionType = zs::SmallString{"move"};
    inputs.push_back(r);
  }
  {
    zs::RecordedInput r;
    r.tick = 3; r.actorId = 1; r.actionType = zs::SmallString{"move"};
    inputs.push_back(r);
  }
  {
    zs::RecordedInput r;
    r.tick = 5; r.actorId = 1; r.actionType = zs::SmallString{"move"};
    inputs.push_back(r);
  }

  zs::InputReplayer replayer;
  replayer.load(inputs);

  assert(replayer.total_inputs() == 3);
  assert(replayer.remaining() == 3);
  assert(replayer.cursor() == 0);
  assert(!replayer.finished());

  // Advance to tick 2 — should execute input at tick 1
  size_t count = replayer.advance_to(2);
  assert(count == 1);
  assert(replayer.cursor() == 1);
  assert(replayer.remaining() == 2);

  // Advance to tick 4 — should execute input at tick 3
  count = replayer.advance_to(4);
  assert(count == 1);
  assert(replayer.cursor() == 2);

  // Advance to tick 10 — should execute input at tick 5
  count = replayer.advance_to(10);
  assert(count == 1);
  assert(replayer.finished());
  assert(replayer.remaining() == 0);

  fprintf(stderr, "[saveload] test_replayer_advance PASS\n");
}

// =====================================================================
//  Test 32: InputReplayer — finished/remaining/cursor
// =====================================================================
static void test_replayer_state() {
  fprintf(stderr, "[saveload] test_replayer_state...\n");

  zs::InputReplayer replayer;

  // Empty replayer
  assert(replayer.finished());
  assert(replayer.remaining() == 0);
  assert(replayer.total_inputs() == 0);
  assert(replayer.cursor() == 0);

  // Load single input
  std::vector<zs::RecordedInput> inputs;
  zs::RecordedInput r;
  r.tick = 1;
  inputs.push_back(r);
  replayer.load(inputs);

  assert(!replayer.finished());
  assert(replayer.remaining() == 1);

  replayer.advance_to(1);
  assert(replayer.finished());

  // Reset
  replayer.reset();
  assert(!replayer.finished());
  assert(replayer.cursor() == 0);

  // Clear
  replayer.clear();
  assert(replayer.finished());
  assert(replayer.total_inputs() == 0);

  fprintf(stderr, "[saveload] test_replayer_state PASS\n");
}

// =====================================================================
//  Test 33: InputReplayer — execute ability through AbilitySystem
// =====================================================================
static void test_replayer_ability_execution() {
  fprintf(stderr, "[saveload] test_replayer_ability_execution...\n");

  zs::AbilitySystem abilities;
  auto desc = make_ability(300, "strike", 0.0);  // 0 cooldown for instant use
  abilities.register_descriptor(desc);

  zs::GameplayEntityId actor{1};
  zs::GameplayEntityId target{2};
  abilities.grant_ability(actor, desc.id);

  zs::GameplayEventDispatcher dispatcher;

  zs::InputRecorder recorder;
  recorder.start(0);
  recorder.record_ability(1, actor.value, desc.id.value, target.value);
  recorder.stop();

  zs::InputReplayer replayer;
  replayer.set_ability_system(&abilities);
  replayer.set_dispatcher(&dispatcher);
  replayer.load_from(recorder);

  assert(replayer.total_inputs() == 1);

  // Before replay, ability should be ready
  auto *inst = abilities.find_instance_by_descriptor(actor, desc.id);
  assert(inst != nullptr);

  // Advance to tick 1 — should trigger the ability
  size_t executed = replayer.advance_to(1);
  assert(executed == 1);
  assert(replayer.finished());

  // The ability should have been activated
  // (state will be active or channeling depending on cast time)
  inst = abilities.find_instance_by_descriptor(actor, desc.id);
  assert(inst != nullptr);

  fprintf(stderr, "[saveload] test_replayer_ability_execution PASS\n");
}

// =====================================================================
//  Test 34: InputReplayer — load_from InputRecorder
// =====================================================================
static void test_replayer_load_from() {
  fprintf(stderr, "[saveload] test_replayer_load_from...\n");

  zs::InputRecorder recorder;
  recorder.start(0);
  recorder.record_ability(1, 1, 100);
  recorder.record_ability(2, 1, 101);
  recorder.record_ability(3, 1, 102);
  recorder.stop();

  zs::InputReplayer replayer;
  replayer.load_from(recorder);

  assert(replayer.total_inputs() == 3);
  assert(!replayer.finished());

  fprintf(stderr, "[saveload] test_replayer_load_from PASS\n");
}

// =====================================================================
//  Test 35: StateComparator — identical snapshots
// =====================================================================
static void test_comparator_identical() {
  fprintf(stderr, "[saveload] test_comparator_identical...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  snap.health.push_back({1, 100.0, 100.0});
  snap.statBlocks.push_back({1, {{zs::SmallString{"str"}, 10.0}}, {}});
  zs::SavedProgressionProfile pp;
  pp.entityId = 1;
  pp.level = 5;
  pp.totalXp = 400;
  snap.progression.push_back(pp);

  zs::StateComparator cmp;
  auto divs = cmp.compare(snap, snap);
  assert(divs.empty());
  assert(cmp.are_identical(snap, snap));

  fprintf(stderr, "[saveload] test_comparator_identical PASS\n");
}

// =====================================================================
//  Test 36: StateComparator — health divergence
// =====================================================================
static void test_comparator_health_divergence() {
  fprintf(stderr, "[saveload] test_comparator_health_divergence...\n");

  zs::GameplayStateSnapshot snapA, snapB;
  snapA.health.push_back({1, 100.0, 100.0});
  snapB.health.push_back({1, 75.0, 100.0});  // Different currentHp

  zs::StateComparator cmp;
  auto divs = cmp.compare(snapA, snapB);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].subsystem.asChars(), "health") == 0);
  assert(std::strcmp(divs[0].fieldName.asChars(), "currentHp") == 0);

  // Missing entity
  zs::GameplayStateSnapshot snapC;
  snapC.health.push_back({1, 100.0, 100.0});
  zs::GameplayStateSnapshot snapD;  // no health

  divs = cmp.compare(snapC, snapD);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].fieldName.asChars(), "presence") == 0);
  assert(std::strcmp(divs[0].expectedValue.asChars(), "present") == 0);
  assert(std::strcmp(divs[0].actualValue.asChars(), "missing") == 0);

  // Extra entity in actual
  divs = cmp.compare(snapD, snapC);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].expectedValue.asChars(), "missing") == 0);
  assert(std::strcmp(divs[0].actualValue.asChars(), "present") == 0);

  fprintf(stderr, "[saveload] test_comparator_health_divergence PASS\n");
}

// =====================================================================
//  Test 37: StateComparator — stat divergence
// =====================================================================
static void test_comparator_stat_divergence() {
  fprintf(stderr, "[saveload] test_comparator_stat_divergence...\n");

  zs::GameplayStateSnapshot snapA, snapB;
  zs::SavedStatBlock sbA;
  sbA.entityId = 1;
  sbA.baseValues.push_back({zs::SmallString{"attack"}, 20.0});
  snapA.statBlocks.push_back(sbA);

  zs::SavedStatBlock sbB;
  sbB.entityId = 1;
  sbB.baseValues.push_back({zs::SmallString{"attack"}, 25.0});  // different
  snapB.statBlocks.push_back(sbB);

  zs::StateComparator cmp;
  auto divs = cmp.compare(snapA, snapB);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].subsystem.asChars(), "stats") == 0);
  assert(std::strcmp(divs[0].fieldName.asChars(), "attack") == 0);

  // Missing stat block
  zs::GameplayStateSnapshot snapC;
  snapC.statBlocks.push_back(sbA);
  zs::GameplayStateSnapshot snapD;  // empty
  divs = cmp.compare(snapC, snapD);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].fieldName.asChars(), "presence") == 0);

  fprintf(stderr, "[saveload] test_comparator_stat_divergence PASS\n");
}

// =====================================================================
//  Test 38: StateComparator — effect count divergence
// =====================================================================
static void test_comparator_effect_divergence() {
  fprintf(stderr, "[saveload] test_comparator_effect_divergence...\n");

  zs::GameplayStateSnapshot snapA, snapB;

  // A has 2 effects on entity 1
  zs::SavedEffectInstance e1;
  e1.targetId = 1;
  e1.descriptorId = 100;
  snapA.effects.push_back(e1);
  snapA.effects.push_back(e1);

  // B has 1 effect on entity 1
  snapB.effects.push_back(e1);

  zs::StateComparator cmp;
  auto divs = cmp.compare(snapA, snapB);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].subsystem.asChars(), "effects") == 0);
  assert(std::strcmp(divs[0].fieldName.asChars(), "count") == 0);
  assert(std::strcmp(divs[0].expectedValue.asChars(), "2") == 0);
  assert(std::strcmp(divs[0].actualValue.asChars(), "1") == 0);

  fprintf(stderr, "[saveload] test_comparator_effect_divergence PASS\n");
}

// =====================================================================
//  Test 39: StateComparator — progression divergence
// =====================================================================
static void test_comparator_progression_divergence() {
  fprintf(stderr, "[saveload] test_comparator_progression_divergence...\n");

  zs::GameplayStateSnapshot snapA, snapB;

  zs::SavedProgressionProfile ppA;
  ppA.entityId = 1;
  ppA.level = 5;
  ppA.totalXp = 400;
  snapA.progression.push_back(ppA);

  zs::SavedProgressionProfile ppB;
  ppB.entityId = 1;
  ppB.level = 6;      // different level
  ppB.totalXp = 500;  // different xp
  snapB.progression.push_back(ppB);

  zs::StateComparator cmp;
  auto divs = cmp.compare(snapA, snapB);
  assert(divs.size() == 2);  // level + totalXp

  bool foundLevel = false, foundXp = false;
  for (auto &d : divs) {
    if (std::strcmp(d.fieldName.asChars(), "level") == 0) foundLevel = true;
    if (std::strcmp(d.fieldName.asChars(), "totalXp") == 0) foundXp = true;
  }
  assert(foundLevel && foundXp);

  // Missing entity in actual
  zs::GameplayStateSnapshot snapC;
  snapC.progression.push_back(ppA);
  zs::GameplayStateSnapshot snapD;

  divs = cmp.compare(snapC, snapD);
  assert(divs.size() == 1);
  assert(std::strcmp(divs[0].fieldName.asChars(), "presence") == 0);

  fprintf(stderr, "[saveload] test_comparator_progression_divergence PASS\n");
}

// =====================================================================
//  Test 40: StateComparator — format_divergences text
// =====================================================================
static void test_format_divergences() {
  fprintf(stderr, "[saveload] test_format_divergences...\n");

  std::vector<zs::StateDivergence> divs;
  zs::StateDivergence d;
  d.subsystem = zs::SmallString{"health"};
  d.entityKey = zs::SmallString{"1"};
  d.fieldName = zs::SmallString{"currentHp"};
  d.expectedValue = zs::SmallString{"100.0"};
  d.actualValue = zs::SmallString{"75.0"};
  divs.push_back(d);

  auto text = zs::StateComparator::format_divergences(divs);
  assert(text.find("1 divergence(s) found:") != std::string::npos);
  assert(text.find("health") != std::string::npos);
  assert(text.find("currentHp") != std::string::npos);
  assert(text.find("100.0") != std::string::npos);
  assert(text.find("75.0") != std::string::npos);

  fprintf(stderr, "[saveload] test_format_divergences PASS\n");
}

// =====================================================================
//  Test 41: StateComparator — format_divergences_json
// =====================================================================
static void test_format_divergences_json() {
  fprintf(stderr, "[saveload] test_format_divergences_json...\n");

  std::vector<zs::StateDivergence> divs;
  zs::StateDivergence d1;
  d1.subsystem = zs::SmallString{"health"};
  d1.entityKey = zs::SmallString{"1"};
  d1.fieldName = zs::SmallString{"currentHp"};
  d1.expectedValue = zs::SmallString{"100.0"};
  d1.actualValue = zs::SmallString{"75.0"};
  divs.push_back(d1);

  zs::StateDivergence d2;
  d2.subsystem = zs::SmallString{"stats"};
  d2.entityKey = zs::SmallString{"1"};
  d2.fieldName = zs::SmallString{"attack"};
  d2.expectedValue = zs::SmallString{"20.0"};
  d2.actualValue = zs::SmallString{"25.0"};
  divs.push_back(d2);

  auto json = zs::StateComparator::format_divergences_json(divs);

  // Should be valid JSON array
  assert(json[0] == '[');
  assert(json[json.size() - 1] == ']');
  assert(json.find("\"subsystem\":\"health\"") != std::string::npos);
  assert(json.find("\"subsystem\":\"stats\"") != std::string::npos);
  assert(json.find("\"field\":\"currentHp\"") != std::string::npos);
  assert(json.find("\"field\":\"attack\"") != std::string::npos);

  // Empty divergences should produce "[]"
  auto emptyJson = zs::StateComparator::format_divergences_json({});
  assert(emptyJson == "[]");

  fprintf(stderr, "[saveload] test_format_divergences_json PASS\n");
}

// =====================================================================
//  Test 42: snapshot_to_json output
// =====================================================================
static void test_snapshot_to_json() {
  fprintf(stderr, "[saveload] test_snapshot_to_json...\n");

  zs::GameplayStateSnapshot snap;
  snap.version = {1, 0};
  snap.label = zs::SmallString{"test_snap"};
  snap.tickCount = 42;
  snap.entityIds = {1, 2};

  snap.health.push_back({1, 90.0, 100.0});

  zs::SavedStatBlock ssb;
  ssb.entityId = 1;
  ssb.baseValues.push_back({zs::SmallString{"str"}, 15.0});
  snap.statBlocks.push_back(ssb);

  zs::SavedEffectInstance se;
  se.instanceId = 1;
  se.descriptorId = 100;
  se.targetId = 1;
  se.sourceId = 2;
  se.durationRemaining = 5.0;
  se.stackCount = 1;
  se.active = true;
  snap.effects.push_back(se);

  zs::SavedAbilityInstance sa;
  sa.instanceId = 1;
  sa.descriptorId = 200;
  sa.ownerId = 1;
  sa.state = 0;
  sa.currentCharges = 1;
  sa.cooldownRemaining = 0.0;
  snap.abilities.push_back(sa);

  zs::SavedProgressionProfile spp;
  spp.entityId = 1;
  spp.level = 3;
  spp.totalXp = 250;
  spp.skillPoints = 2;
  snap.progression.push_back(spp);

  auto json = zs::snapshot_to_json(snap);

  // Verify it's valid-looking JSON with expected fields
  assert(json[0] == '{');
  assert(json[json.size() - 1] == '}');
  assert(json.find("\"version\":{\"major\":1,\"minor\":0}") != std::string::npos);
  assert(json.find("\"label\":\"test_snap\"") != std::string::npos);
  assert(json.find("\"tickCount\":42") != std::string::npos);
  assert(json.find("\"health\":[") != std::string::npos);
  assert(json.find("\"statBlocks\":[") != std::string::npos);
  assert(json.find("\"effects\":[") != std::string::npos);
  assert(json.find("\"abilities\":[") != std::string::npos);
  assert(json.find("\"progression\":[") != std::string::npos);
  assert(json.find("\"str\":15") != std::string::npos);
  assert(json.find("\"active\":true") != std::string::npos);

  // Empty snapshot
  zs::GameplayStateSnapshot empty;
  auto emptyJson = zs::snapshot_to_json(empty);
  assert(emptyJson.find("\"health\":[]") != std::string::npos);
  assert(emptyJson.find("\"effects\":[]") != std::string::npos);

  fprintf(stderr, "[saveload] test_snapshot_to_json PASS\n");
}

// =====================================================================
//  Main
// =====================================================================

int main() {
  fprintf(stderr, "[saveload] === M10 Save/Load Tests (42 tests) ===\n\n");

  test_version_compat();
  test_snapshot_clear();
  test_capture_entity_registration();
  test_capture_health();
  test_capture_stat_block();
  test_capture_effects();
  test_capture_abilities();
  test_capture_progression();
  test_round_trip();
  test_restore_version_mismatch();
  test_restore_health();
  test_restore_stat_block();
  test_restore_effects();
  test_restore_abilities();
  test_restore_progression();
  test_restore_counters();
  test_migration_single();
  test_migration_chain();
  test_migration_failure();
  test_migration_same_version();
  test_migration_no_path();
  test_deterministic_basic();
  test_deterministic_range();
  test_deterministic_repeatability();
  test_deterministic_reset();
  test_deterministic_state();
  test_recorder_startstop();
  test_recorder_record();
  test_recorder_not_recording();
  test_recorder_clear();
  test_replayer_advance();
  test_replayer_state();
  test_replayer_ability_execution();
  test_replayer_load_from();
  test_comparator_identical();
  test_comparator_health_divergence();
  test_comparator_stat_divergence();
  test_comparator_effect_divergence();
  test_comparator_progression_divergence();
  test_format_divergences();
  test_format_divergences_json();
  test_snapshot_to_json();

  fprintf(stderr, "\n[saveload] === ALL 42 TESTS PASSED ===\n");
  return 0;
}
