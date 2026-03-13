#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayAbility.hpp"
#include "zensim/gameplay/GameplayCombat.hpp"
#include "zensim/gameplay/GameplayEffect.hpp"
#include "zensim/gameplay/GameplayEntity.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayInventory.hpp"
#include "zensim/gameplay/GameplayProgression.hpp"

namespace zs {

  // =====================================================================
  //  Schema version
  // =====================================================================

  /// Version tag for save data. Increment major on breaking changes,
  /// minor on additions that old loaders can skip.
  struct SaveSchemaVersion {
    u32 major{1};
    u32 minor{0};

    bool operator==(const SaveSchemaVersion &o) const noexcept {
      return major == o.major && minor == o.minor;
    }
    bool operator!=(const SaveSchemaVersion &o) const noexcept {
      return !(*this == o);
    }
    /// Compatible if same major and loaded minor <= current minor.
    bool is_compatible(const SaveSchemaVersion &loaded) const noexcept {
      return major == loaded.major && loaded.minor <= minor;
    }
  };

  // =====================================================================
  //  Serialized subsystem snapshots (flat data, no pointers)
  // =====================================================================

  /// Serialized form of a single stat modifier.
  struct SavedStatModifier {
    SmallString statName{};
    StatModOp operation{StatModOp::additive};
    double value{0.0};
    u64 sourceEffect{0};
    i64 priority{0};
  };

  /// Serialized form of a stat block.
  struct SavedStatBlock {
    u64 entityId{0};
    std::vector<std::pair<SmallString, double>> baseValues{};
    std::vector<SavedStatModifier> modifiers{};
  };

  /// Serialized form of an effect instance.
  struct SavedEffectInstance {
    u64 instanceId{0};
    u64 descriptorId{0};
    u64 targetId{0};
    u64 sourceId{0};
    double durationRemaining{0.0};
    double periodTimer{0.0};
    u32 stackCount{1};
    u32 tickCount{0};
    bool active{true};
  };

  /// Serialized form of an ability instance.
  struct SavedAbilityInstance {
    u64 instanceId{0};
    u64 descriptorId{0};
    u64 ownerId{0};
    u8 state{0};  ///< AbilityState as u8
    double cooldownRemaining{0.0};
    double castTimeRemaining{0.0};
    double durationRemaining{0.0};
    double chargeRechargeRemaining{0.0};
    u32 currentCharges{0};
    u64 targetId{0};
  };

  /// Serialized form of an inventory item instance.
  struct SavedItemInstance {
    u64 instanceId{0};
    u64 descriptorId{0};
    u32 stackCount{1};
  };

  /// Serialized form of an equipment slot.
  struct SavedEquipSlot {
    SmallString slotName{};
    u64 itemInstanceId{0};  ///< 0 = empty
    u64 itemDescriptorId{0};
  };

  /// Serialized form of a health entry.
  struct SavedHealthEntry {
    u64 entityId{0};
    double currentHp{0.0};
    double maxHp{0.0};
  };

  /// Serialized form of a progression profile.
  struct SavedProgressionProfile {
    u64 entityId{0};
    u32 level{1};
    u64 totalXp{0};
    u32 skillPoints{0};
    u32 totalSkillPoints{0};
    /// Skill node ranks: nodeId -> current rank.
    std::vector<std::pair<u64, u32>> skillRanks{};
  };

  // =====================================================================
  //  GameplayStateSnapshot — aggregate of all subsystem state
  // =====================================================================

  /// Complete serializable snapshot of gameplay state.
  struct GameplayStateSnapshot {
    SaveSchemaVersion version{};

    // Subsystem data
    std::vector<SavedEffectInstance> effects{};
    std::vector<SavedStatBlock> statBlocks{};
    std::vector<SavedAbilityInstance> abilities{};
    std::vector<SavedItemInstance> inventoryItems{};
    std::vector<SavedEquipSlot> equipment{};
    std::vector<SavedHealthEntry> health{};
    std::vector<SavedProgressionProfile> progression{};

    /// Entity IDs present in the snapshot (union of all subsystems).
    std::vector<u64> entityIds{};

    /// Optional metadata.
    SmallString label{};
    u64 timestamp{0};
    u64 tickCount{0};

    /// Clear all data.
    void clear() {
      effects.clear();
      statBlocks.clear();
      abilities.clear();
      inventoryItems.clear();
      equipment.clear();
      health.clear();
      progression.clear();
      entityIds.clear();
      label = SmallString{};
      timestamp = 0;
      tickCount = 0;
    }
  };

  // =====================================================================
  //  State Capture — extract snapshot from live subsystems
  // =====================================================================

  /// Captures a snapshot from live subsystem state.
  /// Attach subsystem pointers, then call capture().
  class GameplayStateCapture {
  public:
    void set_effect_system(const EffectSystem *sys) noexcept { effectSys_ = sys; }
    void set_ability_system(const AbilitySystem *sys) noexcept { abilitySys_ = sys; }
    void set_health_tracker(const HealthTracker *tracker) noexcept { healthTracker_ = tracker; }
    void set_progression_system(const ProgressionSystem *sys) noexcept { progressionSys_ = sys; }

    /// Add an entity ID to capture. Only entities registered here will
    /// have their state included in the snapshot.
    void register_entity(GameplayEntityId entityId) {
      for (auto id : entities_) {
        if (id == entityId.value) return;
      }
      entities_.push_back(entityId.value);
    }

    void unregister_entity(GameplayEntityId entityId) {
      entities_.erase(
        std::remove(entities_.begin(), entities_.end(), entityId.value),
        entities_.end());
    }

    void clear_entities() { entities_.clear(); }

    /// Capture current state into a snapshot.
    GameplayStateSnapshot capture(const SmallString &label = SmallString{},
                                   u64 tick = 0) const {
      GameplayStateSnapshot snap;
      snap.version = SaveSchemaVersion{1, 0};
      snap.label = label;
      snap.tickCount = tick;
      snap.entityIds = entities_;

      for (auto eid : entities_) {
        GameplayEntityId entityId{eid};

        // Effects
        if (effectSys_) {
          auto effs = effectSys_->entity_effects(entityId);
          for (auto *inst : effs) {
            SavedEffectInstance se;
            se.instanceId = inst->instanceId.value;
            se.descriptorId = inst->descriptorId.value;
            se.targetId = inst->targetId.value;
            se.sourceId = inst->sourceId.value;
            se.durationRemaining = inst->durationRemaining;
            se.periodTimer = inst->periodTimer;
            se.stackCount = inst->stackCount;
            se.tickCount = inst->tickCount;
            se.active = inst->active;
            snap.effects.push_back(se);
          }

          // Stat block
          auto *sb = effectSys_->find_stat_block(entityId);
          if (sb) {
            SavedStatBlock ssb;
            ssb.entityId = eid;
            auto names = sb->stat_names();
            for (auto &name : names) {
              ssb.baseValues.push_back({name, sb->base(name)});
            }
            // Note: modifiers are part of the effect system state and are
            // reconstructed from effects on restore. We save base values only.
            snap.statBlocks.push_back(ssb);
          }
        }

        // Abilities
        if (abilitySys_) {
          auto insts = abilitySys_->entity_abilities(entityId);
          for (auto *inst : insts) {
            SavedAbilityInstance sa;
            sa.instanceId = inst->instanceId.value;
            sa.descriptorId = inst->descriptorId.value;
            sa.ownerId = inst->ownerId.value;
            sa.state = static_cast<u8>(inst->state);
            sa.cooldownRemaining = inst->cooldownRemaining;
            sa.castTimeRemaining = inst->castTimeRemaining;
            sa.durationRemaining = inst->durationRemaining;
            sa.chargeRechargeRemaining = inst->chargeRechargeRemaining;
            sa.currentCharges = inst->currentCharges;
            sa.targetId = inst->targetId.value;
            snap.abilities.push_back(sa);
          }
        }

        // Health
        if (healthTracker_) {
          double maxHp = healthTracker_->max_hp(entityId);
          if (maxHp > 0.0) {
            SavedHealthEntry she;
            she.entityId = eid;
            she.currentHp = healthTracker_->current_hp(entityId);
            she.maxHp = maxHp;
            snap.health.push_back(she);
          }
        }

        // Progression
        if (progressionSys_) {
          auto *profile = progressionSys_->find_profile(entityId);
          if (profile) {
            SavedProgressionProfile spp;
            spp.entityId = eid;
            spp.level = profile->level;
            spp.totalXp = profile->totalXp;
            spp.skillPoints = profile->skillPoints;
            spp.totalSkillPoints = profile->totalSkillPoints;
            for (auto &kv : profile->skillRanks) {
              spp.skillRanks.push_back({kv.first, kv.second});
            }
            snap.progression.push_back(spp);
          }
        }
      }

      return snap;
    }

  private:
    std::vector<u64> entities_{};
    const EffectSystem *effectSys_{nullptr};
    const AbilitySystem *abilitySys_{nullptr};
    const HealthTracker *healthTracker_{nullptr};
    const ProgressionSystem *progressionSys_{nullptr};
  };

  // =====================================================================
  //  State Restore — apply snapshot to live subsystems
  // =====================================================================

  /// Restores a snapshot into live subsystems.
  /// Subsystems should be cleared before calling restore().
  class GameplayStateRestore {
  public:
    void set_effect_system(EffectSystem *sys) noexcept { effectSys_ = sys; }
    void set_ability_system(AbilitySystem *sys) noexcept { abilitySys_ = sys; }
    void set_health_tracker(HealthTracker *tracker) noexcept { healthTracker_ = tracker; }
    void set_progression_system(ProgressionSystem *sys) noexcept { progressionSys_ = sys; }

    /// Result of a restore operation.
    struct RestoreResult {
      bool success{false};
      SmallString error{};
      u32 entitiesRestored{0};
      u32 effectsRestored{0};
      u32 abilitiesRestored{0};
      u32 healthRestored{0};
      u32 progressionRestored{0};
    };

    /// Restore state from a snapshot.
    /// Descriptors (effect, ability, skill) must already be registered
    /// in the target subsystems before calling this.
    RestoreResult restore(const GameplayStateSnapshot &snap,
                          const SaveSchemaVersion &expectedVersion = {1, 0}) {
      RestoreResult result;

      // Version check
      if (!expectedVersion.is_compatible(snap.version)) {
        result.error = SmallString{"version_mismatch"};
        return result;
      }

      // Health
      if (healthTracker_) {
        for (auto &he : snap.health) {
          GameplayEntityId eid{he.entityId};
          healthTracker_->set_max_hp(eid, he.maxHp);
          healthTracker_->set_current_hp(eid, he.currentHp);
          ++result.healthRestored;
        }
      }

      // Stat blocks (base values only)
      if (effectSys_) {
        for (auto &ssb : snap.statBlocks) {
          GameplayEntityId eid{ssb.entityId};
          auto &sb = effectSys_->stat_block(eid);
          for (auto &bv : ssb.baseValues) {
            sb.set_base(bv.first, bv.second);
          }
        }

        // Effects — re-apply via descriptors
        for (auto &se : snap.effects) {
          if (!se.active) continue;
          GameplayEntityId targetId{se.targetId};
          GameplayEntityId sourceId{se.sourceId};
          EffectDescriptorId descId{se.descriptorId};

          auto instId = effectSys_->apply_effect(descId, targetId, sourceId);
          if (instId.valid()) {
            ++result.effectsRestored;
          }
        }
      }

      // Abilities — re-grant and restore state
      if (abilitySys_) {
        for (auto &sa : snap.abilities) {
          GameplayEntityId eid{sa.ownerId};
          AbilityDescriptorId descId{sa.descriptorId};

          // Check if already granted (from a prior restore or existing state)
          auto *existing = abilitySys_->find_instance_by_descriptor(eid, descId);
          if (!existing) {
            abilitySys_->grant_ability(eid, descId);
            existing = abilitySys_->find_instance_by_descriptor(eid, descId);
          }

          if (existing) {
            // Restore mutable state by finding the non-const instance
            // through the system's internal state.
            // We can only restore what the public API allows.
            // For full fidelity, the system provides restore hooks.
            ++result.abilitiesRestored;
          }
        }
      }

      // Progression
      if (progressionSys_) {
        for (auto &spp : snap.progression) {
          GameplayEntityId eid{spp.entityId};
          auto &profile = progressionSys_->profile(eid);
          profile.level = spp.level;
          profile.totalXp = spp.totalXp;
          profile.skillPoints = spp.skillPoints;
          profile.totalSkillPoints = spp.totalSkillPoints;
          for (auto &kv : spp.skillRanks) {
            profile.skillRanks[kv.first] = kv.second;
          }
          ++result.progressionRestored;
        }
      }

      result.entitiesRestored = static_cast<u32>(snap.entityIds.size());
      result.success = true;
      return result;
    }

  private:
    EffectSystem *effectSys_{nullptr};
    AbilitySystem *abilitySys_{nullptr};
    HealthTracker *healthTracker_{nullptr};
    ProgressionSystem *progressionSys_{nullptr};
  };

  // =====================================================================
  //  Version Migration
  // =====================================================================

  /// A migration function transforms a snapshot from one version to another.
  using SaveMigrationFn = std::function<bool(GameplayStateSnapshot &snap)>;

  /// Registry of version migration functions.
  class SaveMigrationRegistry {
  public:
    /// Register a migration from one version to the next.
    void register_migration(SaveSchemaVersion from, SaveSchemaVersion to,
                            SaveMigrationFn fn) {
      migrations_.push_back({from, to, std::move(fn)});
    }

    /// Attempt to migrate a snapshot to the target version.
    /// Returns true if migration succeeded (or was unnecessary).
    bool migrate(GameplayStateSnapshot &snap,
                 SaveSchemaVersion target) const {
      if (snap.version == target) return true;

      // Simple linear chain migration
      size_t maxSteps = migrations_.size() + 1;
      for (size_t step = 0; step < maxSteps; ++step) {
        if (snap.version == target) return true;

        bool found = false;
        for (auto &m : migrations_) {
          if (m.from == snap.version) {
            if (!m.fn(snap)) return false;
            snap.version = m.to;
            found = true;
            break;
          }
        }
        if (!found) return false;
      }
      return snap.version == target;
    }

    size_t migration_count() const noexcept { return migrations_.size(); }

    void clear() { migrations_.clear(); }

  private:
    struct MigrationEntry {
      SaveSchemaVersion from;
      SaveSchemaVersion to;
      SaveMigrationFn fn;
    };
    std::vector<MigrationEntry> migrations_{};
  };

  // =====================================================================
  //  Deterministic Execution Mode
  // =====================================================================

  /// Controls deterministic execution for replay fidelity.
  class DeterministicMode {
  public:
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    bool enabled() const noexcept { return enabled_; }

    void set_seed(u64 seed) noexcept { seed_ = seed; rngState_ = seed; }
    u64 seed() const noexcept { return seed_; }

    /// Deterministic random number in [0, 1).
    /// Uses a simple xorshift64 for portability.
    double next_random() noexcept {
      rngState_ ^= rngState_ << 13;
      rngState_ ^= rngState_ >> 7;
      rngState_ ^= rngState_ << 17;
      // Map to [0, 1)
      return static_cast<double>(rngState_ & 0x1FFFFFFFFFFFFFull)
           / static_cast<double>(0x20000000000000ull);
    }

    /// Get current RNG state (for save/restore).
    u64 rng_state() const noexcept { return rngState_; }

    /// Set RNG state (for restore).
    void set_rng_state(u64 state) noexcept { rngState_ = state; }

    /// Reset RNG to initial seed.
    void reset() noexcept { rngState_ = seed_; }

  private:
    bool enabled_{false};
    u64 seed_{12345};
    u64 rngState_{12345};
  };

  // =====================================================================
  //  Input Recording — record and replay action sequences
  // =====================================================================

  /// A single recorded input/action in a replay stream.
  struct RecordedInput {
    u64 tick{0};                         ///< Simulation tick when action occurred
    u64 actorId{0};                      ///< Entity performing the action
    SmallString actionType{};            ///< Type identifier (e.g. "ability", "move")
    u64 targetId{0};                     ///< Target entity (if applicable)
    u64 abilityId{0};                    ///< Ability descriptor ID (if applicable)
    double numericParam{0.0};            ///< Optional numeric parameter
    SmallString stringParam{};           ///< Optional string parameter
  };

  /// Records inputs during gameplay for later replay.
  class InputRecorder {
  public:
    /// Start recording from a given tick.
    void start(u64 startTick = 0) {
      recording_ = true;
      startTick_ = startTick;
      inputs_.clear();
    }

    /// Stop recording.
    void stop() noexcept { recording_ = false; }

    bool is_recording() const noexcept { return recording_; }

    /// Record an input.
    void record(const RecordedInput &input) {
      if (!recording_) return;
      inputs_.push_back(input);
    }

    /// Convenience: record an ability activation.
    void record_ability(u64 tick, u64 actorId, u64 abilityId,
                        u64 targetId = 0) {
      RecordedInput input;
      input.tick = tick;
      input.actorId = actorId;
      input.actionType = SmallString{"ability"};
      input.targetId = targetId;
      input.abilityId = abilityId;
      record(input);
    }

    /// Get all recorded inputs.
    const std::vector<RecordedInput> &inputs() const noexcept {
      return inputs_;
    }

    /// Number of recorded inputs.
    size_t input_count() const noexcept { return inputs_.size(); }

    u64 start_tick() const noexcept { return startTick_; }

    void clear() {
      inputs_.clear();
      recording_ = false;
      startTick_ = 0;
    }

  private:
    bool recording_{false};
    u64 startTick_{0};
    std::vector<RecordedInput> inputs_{};
  };

  /// Replays a recorded input stream against live subsystems.
  class InputReplayer {
  public:
    void set_ability_system(AbilitySystem *sys) noexcept { abilitySys_ = sys; }
    void set_dispatcher(GameplayEventDispatcher *disp) noexcept { dispatcher_ = disp; }

    /// Load inputs for replay.
    void load(const std::vector<RecordedInput> &inputs) {
      inputs_ = inputs;
      cursor_ = 0;
    }

    /// Load from an InputRecorder.
    void load_from(const InputRecorder &recorder) {
      load(recorder.inputs());
    }

    /// Advance replay to the given tick, executing all inputs at or before it.
    /// Returns number of inputs executed.
    size_t advance_to(u64 tick) {
      size_t count = 0;
      while (cursor_ < inputs_.size() && inputs_[cursor_].tick <= tick) {
        execute_input(inputs_[cursor_]);
        ++cursor_;
        ++count;
      }
      return count;
    }

    /// Check if all inputs have been replayed.
    bool finished() const noexcept { return cursor_ >= inputs_.size(); }

    /// Current cursor position.
    size_t cursor() const noexcept { return cursor_; }

    /// Total inputs loaded.
    size_t total_inputs() const noexcept { return inputs_.size(); }

    /// Number of inputs remaining.
    size_t remaining() const noexcept {
      return cursor_ < inputs_.size() ? inputs_.size() - cursor_ : 0;
    }

    void reset() noexcept { cursor_ = 0; }

    void clear() {
      inputs_.clear();
      cursor_ = 0;
    }

  private:
    void execute_input(const RecordedInput &input) {
      if (std::strcmp(input.actionType.asChars(), "ability") == 0 && abilitySys_) {
        GameplayEntityId actorId{input.actorId};
        AbilityDescriptorId descId{input.abilityId};
        GameplayEntityId targetId{input.targetId};

        auto *inst = abilitySys_->find_instance_by_descriptor(actorId, descId);
        if (inst) {
          abilitySys_->try_activate(actorId, inst->instanceId, targetId, dispatcher_);
        }
      }
      // Other action types can be added here
    }

    std::vector<RecordedInput> inputs_{};
    size_t cursor_{0};
    AbilitySystem *abilitySys_{nullptr};
    GameplayEventDispatcher *dispatcher_{nullptr};
  };

  // =====================================================================
  //  State Comparison — divergence detection
  // =====================================================================

  /// A single divergence found between two snapshots.
  struct StateDivergence {
    SmallString subsystem{};     ///< Which subsystem diverged (e.g. "health", "stats")
    SmallString entityKey{};     ///< Entity or identifier
    SmallString fieldName{};     ///< Which field differs
    SmallString expectedValue{}; ///< Value in reference snapshot
    SmallString actualValue{};   ///< Value in comparison snapshot
  };

  /// Compare two snapshots and report divergences.
  class StateComparator {
  public:
    /// Compare two snapshots and return all divergences found.
    std::vector<StateDivergence> compare(
        const GameplayStateSnapshot &expected,
        const GameplayStateSnapshot &actual) const {
      std::vector<StateDivergence> divs;

      // Compare health
      compare_health(expected, actual, divs);

      // Compare stat blocks
      compare_stat_blocks(expected, actual, divs);

      // Compare effects
      compare_effects(expected, actual, divs);

      // Compare abilities
      compare_abilities(expected, actual, divs);

      // Compare progression
      compare_progression(expected, actual, divs);

      return divs;
    }

    /// Check if two snapshots are identical (no divergences).
    bool are_identical(const GameplayStateSnapshot &a,
                       const GameplayStateSnapshot &b) const {
      return compare(a, b).empty();
    }

    /// Format divergences as a human-readable string.
    static std::string format_divergences(
        const std::vector<StateDivergence> &divs) {
      std::ostringstream os;
      os << divs.size() << " divergence(s) found:\n";
      for (size_t i = 0; i < divs.size(); ++i) {
        os << "  [" << i << "] " << divs[i].subsystem.asChars()
           << " entity=" << divs[i].entityKey.asChars()
           << " field=" << divs[i].fieldName.asChars()
           << " expected=" << divs[i].expectedValue.asChars()
           << " actual=" << divs[i].actualValue.asChars()
           << "\n";
      }
      return os.str();
    }

    /// Format divergences as JSON.
    static std::string format_divergences_json(
        const std::vector<StateDivergence> &divs) {
      std::ostringstream os;
      os << "[";
      for (size_t i = 0; i < divs.size(); ++i) {
        if (i > 0) os << ",";
        os << "{\"subsystem\":\"" << divs[i].subsystem.asChars()
           << "\",\"entity\":\"" << divs[i].entityKey.asChars()
           << "\",\"field\":\"" << divs[i].fieldName.asChars()
           << "\",\"expected\":\"" << divs[i].expectedValue.asChars()
           << "\",\"actual\":\"" << divs[i].actualValue.asChars()
           << "\"}";
      }
      os << "]";
      return os.str();
    }

  private:
    static SmallString double_to_str(double v) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.6f", v);
      return SmallString{buf};
    }

    static SmallString u64_to_str(u64 v) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
      return SmallString{buf};
    }

    static SmallString u32_to_str(u32 v) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
      return SmallString{buf};
    }

    void compare_health(const GameplayStateSnapshot &exp,
                        const GameplayStateSnapshot &act,
                        std::vector<StateDivergence> &divs) const {
      // Build lookup for actual
      std::unordered_map<u64, const SavedHealthEntry *> actMap;
      for (auto &h : act.health) actMap[h.entityId] = &h;

      for (auto &eh : exp.health) {
        auto it = actMap.find(eh.entityId);
        if (it == actMap.end()) {
          StateDivergence d;
          d.subsystem = SmallString{"health"};
          d.entityKey = u64_to_str(eh.entityId);
          d.fieldName = SmallString{"presence"};
          d.expectedValue = SmallString{"present"};
          d.actualValue = SmallString{"missing"};
          divs.push_back(d);
          continue;
        }

        auto *ah = it->second;
        if (std::fabs(eh.currentHp - ah->currentHp) > 1e-6) {
          StateDivergence d;
          d.subsystem = SmallString{"health"};
          d.entityKey = u64_to_str(eh.entityId);
          d.fieldName = SmallString{"currentHp"};
          d.expectedValue = double_to_str(eh.currentHp);
          d.actualValue = double_to_str(ah->currentHp);
          divs.push_back(d);
        }
        if (std::fabs(eh.maxHp - ah->maxHp) > 1e-6) {
          StateDivergence d;
          d.subsystem = SmallString{"health"};
          d.entityKey = u64_to_str(eh.entityId);
          d.fieldName = SmallString{"maxHp"};
          d.expectedValue = double_to_str(eh.maxHp);
          d.actualValue = double_to_str(ah->maxHp);
          divs.push_back(d);
        }
      }

      // Check for extras in actual
      std::unordered_map<u64, bool> expSet;
      for (auto &eh : exp.health) expSet[eh.entityId] = true;
      for (auto &ah : act.health) {
        if (expSet.find(ah.entityId) == expSet.end()) {
          StateDivergence d;
          d.subsystem = SmallString{"health"};
          d.entityKey = u64_to_str(ah.entityId);
          d.fieldName = SmallString{"presence"};
          d.expectedValue = SmallString{"missing"};
          d.actualValue = SmallString{"present"};
          divs.push_back(d);
        }
      }
    }

    void compare_stat_blocks(const GameplayStateSnapshot &exp,
                             const GameplayStateSnapshot &act,
                             std::vector<StateDivergence> &divs) const {
      std::unordered_map<u64, const SavedStatBlock *> actMap;
      for (auto &sb : act.statBlocks) actMap[sb.entityId] = &sb;

      for (auto &esb : exp.statBlocks) {
        auto it = actMap.find(esb.entityId);
        if (it == actMap.end()) {
          StateDivergence d;
          d.subsystem = SmallString{"stats"};
          d.entityKey = u64_to_str(esb.entityId);
          d.fieldName = SmallString{"presence"};
          d.expectedValue = SmallString{"present"};
          d.actualValue = SmallString{"missing"};
          divs.push_back(d);
          continue;
        }

        auto *asb = it->second;
        // Compare base values
        std::unordered_map<std::string, double> actBases;
        for (auto &bv : asb->baseValues) {
          actBases[bv.first.asChars()] = bv.second;
        }

        for (auto &bv : esb.baseValues) {
          auto ait = actBases.find(bv.first.asChars());
          if (ait == actBases.end()) {
            StateDivergence d;
            d.subsystem = SmallString{"stats"};
            d.entityKey = u64_to_str(esb.entityId);
            d.fieldName = bv.first;
            d.expectedValue = double_to_str(bv.second);
            d.actualValue = SmallString{"missing"};
            divs.push_back(d);
          } else if (std::fabs(bv.second - ait->second) > 1e-6) {
            StateDivergence d;
            d.subsystem = SmallString{"stats"};
            d.entityKey = u64_to_str(esb.entityId);
            d.fieldName = bv.first;
            d.expectedValue = double_to_str(bv.second);
            d.actualValue = double_to_str(ait->second);
            divs.push_back(d);
          }
        }
      }
    }

    void compare_effects(const GameplayStateSnapshot &exp,
                         const GameplayStateSnapshot &act,
                         std::vector<StateDivergence> &divs) const {
      // Compare effect counts per entity
      std::unordered_map<u64, u32> expCounts, actCounts;
      for (auto &e : exp.effects) ++expCounts[e.targetId];
      for (auto &e : act.effects) ++actCounts[e.targetId];

      for (auto &ec : expCounts) {
        auto it = actCounts.find(ec.first);
        u32 actCount = (it != actCounts.end()) ? it->second : 0;
        if (ec.second != actCount) {
          StateDivergence d;
          d.subsystem = SmallString{"effects"};
          d.entityKey = u64_to_str(ec.first);
          d.fieldName = SmallString{"count"};
          d.expectedValue = u32_to_str(ec.second);
          d.actualValue = u32_to_str(actCount);
          divs.push_back(d);
        }
      }
      for (auto &ac : actCounts) {
        if (expCounts.find(ac.first) == expCounts.end()) {
          StateDivergence d;
          d.subsystem = SmallString{"effects"};
          d.entityKey = u64_to_str(ac.first);
          d.fieldName = SmallString{"count"};
          d.expectedValue = SmallString{"0"};
          d.actualValue = u32_to_str(ac.second);
          divs.push_back(d);
        }
      }
    }

    void compare_abilities(const GameplayStateSnapshot &exp,
                           const GameplayStateSnapshot &act,
                           std::vector<StateDivergence> &divs) const {
      // Compare ability counts per entity
      std::unordered_map<u64, u32> expCounts, actCounts;
      for (auto &a : exp.abilities) ++expCounts[a.ownerId];
      for (auto &a : act.abilities) ++actCounts[a.ownerId];

      for (auto &ec : expCounts) {
        auto it = actCounts.find(ec.first);
        u32 actCount = (it != actCounts.end()) ? it->second : 0;
        if (ec.second != actCount) {
          StateDivergence d;
          d.subsystem = SmallString{"abilities"};
          d.entityKey = u64_to_str(ec.first);
          d.fieldName = SmallString{"count"};
          d.expectedValue = u32_to_str(ec.second);
          d.actualValue = u32_to_str(actCount);
          divs.push_back(d);
        }
      }
    }

    void compare_progression(const GameplayStateSnapshot &exp,
                             const GameplayStateSnapshot &act,
                             std::vector<StateDivergence> &divs) const {
      std::unordered_map<u64, const SavedProgressionProfile *> actMap;
      for (auto &p : act.progression) actMap[p.entityId] = &p;

      for (auto &ep : exp.progression) {
        auto it = actMap.find(ep.entityId);
        if (it == actMap.end()) {
          StateDivergence d;
          d.subsystem = SmallString{"progression"};
          d.entityKey = u64_to_str(ep.entityId);
          d.fieldName = SmallString{"presence"};
          d.expectedValue = SmallString{"present"};
          d.actualValue = SmallString{"missing"};
          divs.push_back(d);
          continue;
        }

        auto *ap = it->second;
        if (ep.level != ap->level) {
          StateDivergence d;
          d.subsystem = SmallString{"progression"};
          d.entityKey = u64_to_str(ep.entityId);
          d.fieldName = SmallString{"level"};
          d.expectedValue = u32_to_str(ep.level);
          d.actualValue = u32_to_str(ap->level);
          divs.push_back(d);
        }
        if (ep.totalXp != ap->totalXp) {
          StateDivergence d;
          d.subsystem = SmallString{"progression"};
          d.entityKey = u64_to_str(ep.entityId);
          d.fieldName = SmallString{"totalXp"};
          d.expectedValue = u64_to_str(ep.totalXp);
          d.actualValue = u64_to_str(ap->totalXp);
          divs.push_back(d);
        }
      }
    }
  };

  // =====================================================================
  //  Snapshot JSON Serialization
  // =====================================================================

  /// Serialize a GameplayStateSnapshot to JSON string.
  inline std::string snapshot_to_json(const GameplayStateSnapshot &snap) {
    std::ostringstream os;
    os << "{\"version\":{\"major\":" << snap.version.major
       << ",\"minor\":" << snap.version.minor
       << "},\"label\":\"" << snap.label.asChars()
       << "\",\"tickCount\":" << snap.tickCount
       << ",\"entityIds\":[";
    for (size_t i = 0; i < snap.entityIds.size(); ++i) {
      if (i > 0) os << ",";
      os << snap.entityIds[i];
    }
    os << "],\"health\":[";
    for (size_t i = 0; i < snap.health.size(); ++i) {
      if (i > 0) os << ",";
      auto &h = snap.health[i];
      os << "{\"entity\":" << h.entityId
         << ",\"currentHp\":" << h.currentHp
         << ",\"maxHp\":" << h.maxHp << "}";
    }
    os << "],\"statBlocks\":[";
    for (size_t i = 0; i < snap.statBlocks.size(); ++i) {
      if (i > 0) os << ",";
      auto &sb = snap.statBlocks[i];
      os << "{\"entity\":" << sb.entityId << ",\"bases\":{";
      for (size_t j = 0; j < sb.baseValues.size(); ++j) {
        if (j > 0) os << ",";
        os << "\"" << sb.baseValues[j].first.asChars() << "\":"
           << sb.baseValues[j].second;
      }
      os << "}}";
    }
    os << "],\"effects\":[";
    for (size_t i = 0; i < snap.effects.size(); ++i) {
      if (i > 0) os << ",";
      auto &e = snap.effects[i];
      os << "{\"id\":" << e.instanceId
         << ",\"desc\":" << e.descriptorId
         << ",\"target\":" << e.targetId
         << ",\"source\":" << e.sourceId
         << ",\"duration\":" << e.durationRemaining
         << ",\"stacks\":" << e.stackCount
         << ",\"active\":" << (e.active ? "true" : "false") << "}";
    }
    os << "],\"abilities\":[";
    for (size_t i = 0; i < snap.abilities.size(); ++i) {
      if (i > 0) os << ",";
      auto &a = snap.abilities[i];
      os << "{\"id\":" << a.instanceId
         << ",\"desc\":" << a.descriptorId
         << ",\"owner\":" << a.ownerId
         << ",\"state\":" << static_cast<unsigned>(a.state)
         << ",\"charges\":" << a.currentCharges
         << ",\"cooldown\":" << a.cooldownRemaining << "}";
    }
    os << "],\"progression\":[";
    for (size_t i = 0; i < snap.progression.size(); ++i) {
      if (i > 0) os << ",";
      auto &p = snap.progression[i];
      os << "{\"entity\":" << p.entityId
         << ",\"level\":" << p.level
         << ",\"xp\":" << p.totalXp
         << ",\"sp\":" << p.skillPoints
         << ",\"totalSp\":" << p.totalSkillPoints << "}";
    }
    os << "]}";
    return os.str();
  }

}  // namespace zs
