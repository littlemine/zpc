#pragma once

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayAbility.hpp"
#include "zensim/gameplay/GameplayEffect.hpp"
#include "zensim/gameplay/GameplayEntity.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayTag.hpp"

namespace zs {

  // ---- Combat event type IDs ----

  namespace combat_events {
    constexpr GameplayEventTypeId damage_dealt{300};
    constexpr GameplayEventTypeId damage_received{301};
    constexpr GameplayEventTypeId healing_dealt{302};
    constexpr GameplayEventTypeId healing_received{303};
    constexpr GameplayEventTypeId combat_started{304};
    constexpr GameplayEventTypeId combat_ended{305};
    constexpr GameplayEventTypeId attack_missed{306};
    constexpr GameplayEventTypeId attack_blocked{307};
    constexpr GameplayEventTypeId attack_critical{308};
    constexpr GameplayEventTypeId effect_applied_in_combat{309};
    constexpr GameplayEventTypeId pipeline_stage_completed{310};
    constexpr GameplayEventTypeId kill{311};
  }  // namespace combat_events

  // ---- Combat enumerations ----

  /// The outcome of a single combat action.
  enum class CombatOutcome : u8 {
    hit,        ///< Normal hit
    miss,       ///< Attack missed
    blocked,    ///< Attack was blocked
    critical,   ///< Critical hit
    evaded,     ///< Target evaded
    immune      ///< Target was immune
  };

  inline const char *combat_outcome_name(CombatOutcome o) noexcept {
    switch (o) {
      case CombatOutcome::hit: return "hit";
      case CombatOutcome::miss: return "miss";
      case CombatOutcome::blocked: return "blocked";
      case CombatOutcome::critical: return "critical";
      case CombatOutcome::evaded: return "evaded";
      case CombatOutcome::immune: return "immune";
      default: return "unknown";
    }
  }

  /// Type of damage or healing.
  enum class DamageType : u8 {
    physical,     ///< Mitigated by armor
    magical,      ///< Mitigated by magic resistance
    pure,         ///< Not mitigated
    healing       ///< Restores health (not mitigated)
  };

  inline const char *damage_type_name(DamageType t) noexcept {
    switch (t) {
      case DamageType::physical: return "physical";
      case DamageType::magical: return "magical";
      case DamageType::pure: return "pure";
      case DamageType::healing: return "healing";
      default: return "unknown";
    }
  }

  /// Identifies which stage of the combat pipeline is executing.
  enum class CombatStage : u8 {
    pre_calculation,    ///< Hit/miss/crit rolls, pre-modifiers
    damage_calculation, ///< Base damage/heal calculation
    mitigation,         ///< Armor, resistance, shields
    post_calculation,   ///< Final adjustments, clamping
    effect_application, ///< Status effect application
    event_emission      ///< Emit events for telemetry/UI
  };

  inline const char *combat_stage_name(CombatStage s) noexcept {
    switch (s) {
      case CombatStage::pre_calculation: return "pre_calculation";
      case CombatStage::damage_calculation: return "damage_calculation";
      case CombatStage::mitigation: return "mitigation";
      case CombatStage::post_calculation: return "post_calculation";
      case CombatStage::effect_application: return "effect_application";
      case CombatStage::event_emission: return "event_emission";
      default: return "unknown";
    }
  }

  // ---- Combat action and result ----

  /// Describes a single combat action to be resolved through the pipeline.
  ///
  /// A CombatAction is an input to the combat pipeline. It describes who
  /// is attacking/healing, who the target is, and the initial parameters.
  struct CombatAction {
    GameplayEntityId attackerId{};       ///< Source entity
    GameplayEntityId defenderId{};       ///< Target entity
    AbilityDescriptorId abilityId{};     ///< Ability that triggered this action (optional)
    DamageType damageType{DamageType::physical};
    SmallString actionName{};            ///< Descriptive name (e.g., "Fireball", "Slash")
    GameplayTagContainer tags{};         ///< Tags for filtering / stage hooks

    // Initial values (set by caller)
    double basePower{0.0};               ///< Raw power before any calculation
    double critChance{0.0};              ///< Critical hit chance [0.0, 1.0]
    double critMultiplier{2.0};          ///< Damage multiplier on critical hit
    double hitChance{1.0};               ///< Hit chance [0.0, 1.0]
    double blockChance{0.0};             ///< Block chance [0.0, 1.0] (defender's stat)

    // Effects to apply on hit
    std::vector<EffectDescriptorId> onHitEffects{};

    /// Whether this action is a heal (convenience check).
    bool is_healing() const noexcept { return damageType == DamageType::healing; }
  };

  /// The result of resolving a combat action through the pipeline.
  ///
  /// CombatResult is built up incrementally as the action passes through
  /// pipeline stages. After the pipeline completes, this contains the
  /// full outcome data.
  struct CombatResult {
    CombatOutcome outcome{CombatOutcome::hit};

    // Damage / healing values at each stage
    double rawDamage{0.0};           ///< After damage_calculation (before mitigation)
    double mitigatedAmount{0.0};     ///< Amount absorbed by mitigation
    double finalDamage{0.0};         ///< After mitigation and post_calculation

    // For healing
    double rawHealing{0.0};
    double finalHealing{0.0};

    // Combat details
    bool isCritical{false};
    bool isBlocked{false};
    bool isMiss{false};

    // Effects applied during combat
    std::vector<EffectInstanceId> appliedEffects{};

    // Source data (copied from action for reference)
    GameplayEntityId attackerId{};
    GameplayEntityId defenderId{};
    DamageType damageType{DamageType::physical};
    SmallString actionName{};

    /// Net value: positive = damage dealt, negative = healing done.
    double net_value() const noexcept {
      if (damageType == DamageType::healing) return -finalHealing;
      return finalDamage;
    }
  };

  // ---- Combat context (passed through pipeline stages) ----

  /// Mutable context flowing through the combat pipeline. Stages can
  /// read/modify the action, result, and access game systems.
  struct CombatContext {
    CombatAction action{};
    CombatResult result{};

    // System references (set by CombatPipeline before execution)
    EffectSystem *effectSystem{nullptr};
    AbilitySystem *abilitySystem{nullptr};
    GameplayEventDispatcher *dispatcher{nullptr};

    // Per-entity stat accessors (convenience pointers)
    const StatBlock *attackerStats{nullptr};
    const StatBlock *defenderStats{nullptr};

    // Random value for deterministic testing. If >= 0, used instead of
    // random rolls. Set to < 0 (default) for real random behavior.
    double deterministicRoll{-1.0};

    /// Get a roll value in [0.0, 1.0). Uses deterministicRoll if >= 0,
    /// otherwise generates a pseudo-random value.
    double roll() const noexcept {
      if (deterministicRoll >= 0.0) return deterministicRoll;
      // Simple LCG-based fallback (not cryptographic, fine for gameplay)
      static u64 seed = 12345;
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      return static_cast<double>(seed >> 33) / static_cast<double>(1ULL << 31);
    }
  };

  // ---- Pipeline stage function ----

  /// A pipeline stage function receives the mutable context and can modify
  /// the action, result, and interact with game systems.
  /// Returns true to continue the pipeline, false to abort (short-circuit).
  using CombatStageFn = std::function<bool(CombatContext &ctx)>;

  /// A registered pipeline stage entry with metadata.
  struct CombatStageEntry {
    CombatStage stage{CombatStage::pre_calculation};
    SmallString name{};        ///< Human-readable stage name
    CombatStageFn handler{};
    i64 priority{0};           ///< Lower priority runs first within a stage
  };

  // ---- Default stage implementations ----

  /// Default pre-calculation stage: resolves hit/miss/crit/block rolls.
  inline bool default_pre_calculation(CombatContext &ctx) {
    auto &action = ctx.action;
    auto &result = ctx.result;

    // For healing, skip hit/miss/crit rolls
    if (action.is_healing()) return true;

    double rollValue = ctx.roll();

    // Miss check
    if (rollValue >= action.hitChance) {
      result.outcome = CombatOutcome::miss;
      result.isMiss = true;
      return true;  // Continue pipeline; later stages should check isMiss
    }

    // Block check (second roll)
    double blockRoll = ctx.roll();
    if (blockRoll < action.blockChance) {
      result.outcome = CombatOutcome::blocked;
      result.isBlocked = true;
      return true;
    }

    // Critical hit check (third roll)
    double critRoll = ctx.roll();
    if (critRoll < action.critChance) {
      result.outcome = CombatOutcome::critical;
      result.isCritical = true;
    }

    return true;
  }

  /// Default damage calculation stage: computes raw damage from base power
  /// and attacker stats.
  inline bool default_damage_calculation(CombatContext &ctx) {
    auto &action = ctx.action;
    auto &result = ctx.result;

    // Short-circuit on miss
    if (result.isMiss) return true;

    if (action.is_healing()) {
      // Healing: base power as raw healing
      result.rawHealing = action.basePower;

      // Scale by attacker's healing_power stat if available
      if (ctx.attackerStats) {
        double healingPower = ctx.attackerStats->compute(SmallString{"healing_power"});
        if (healingPower > 0.0) {
          result.rawHealing *= (1.0 + healingPower / 100.0);
        }
      }

      return true;
    }

    // Damage calculation
    result.rawDamage = action.basePower;

    // Scale by attacker's attack_power stat if available
    if (ctx.attackerStats) {
      double attackPower = ctx.attackerStats->compute(SmallString{"attack_power"});
      if (attackPower > 0.0) {
        result.rawDamage *= (1.0 + attackPower / 100.0);
      }
    }

    // Apply critical multiplier
    if (result.isCritical) {
      result.rawDamage *= action.critMultiplier;
    }

    // Blocked attacks deal reduced damage (50% by default)
    if (result.isBlocked) {
      result.rawDamage *= 0.5;
    }

    return true;
  }

  /// Default mitigation stage: applies armor/resistance reduction.
  inline bool default_mitigation(CombatContext &ctx) {
    auto &action = ctx.action;
    auto &result = ctx.result;

    // Short-circuit on miss
    if (result.isMiss) return true;

    if (action.is_healing()) {
      // No mitigation for healing
      result.finalHealing = result.rawHealing;
      return true;
    }

    // Pure damage bypasses mitigation
    if (action.damageType == DamageType::pure) {
      result.finalDamage = result.rawDamage;
      result.mitigatedAmount = 0.0;
      return true;
    }

    double mitigation = 0.0;

    if (ctx.defenderStats) {
      if (action.damageType == DamageType::physical) {
        // Armor reduces physical damage: reduction = armor / (armor + 100)
        double armor = ctx.defenderStats->compute(SmallString{"armor"});
        if (armor > 0.0) {
          mitigation = armor / (armor + 100.0);
        }
      } else if (action.damageType == DamageType::magical) {
        // Magic resistance reduces magical damage: reduction = mr / (mr + 100)
        double magicResist = ctx.defenderStats->compute(SmallString{"magic_resistance"});
        if (magicResist > 0.0) {
          mitigation = magicResist / (magicResist + 100.0);
        }
      }
    }

    result.mitigatedAmount = result.rawDamage * mitigation;
    result.finalDamage = result.rawDamage - result.mitigatedAmount;

    // Floor at zero
    if (result.finalDamage < 0.0) result.finalDamage = 0.0;

    return true;
  }

  /// Default post-calculation stage: final adjustments and clamping.
  inline bool default_post_calculation(CombatContext &ctx) {
    auto &result = ctx.result;

    // On miss: zero out everything
    if (result.isMiss) {
      result.rawDamage = 0.0;
      result.mitigatedAmount = 0.0;
      result.finalDamage = 0.0;
      result.rawHealing = 0.0;
      result.finalHealing = 0.0;
      return true;
    }

    // Ensure non-negative values
    if (result.finalDamage < 0.0) result.finalDamage = 0.0;
    if (result.finalHealing < 0.0) result.finalHealing = 0.0;

    return true;
  }

  /// Default effect application stage: applies on-hit effects to the target.
  inline bool default_effect_application(CombatContext &ctx) {
    auto &action = ctx.action;
    auto &result = ctx.result;

    // Don't apply effects on miss
    if (result.isMiss) return true;

    if (!ctx.effectSystem) return true;
    if (action.onHitEffects.empty()) return true;

    for (const auto &effectId : action.onHitEffects) {
      auto instId = ctx.effectSystem->apply_effect(
          effectId, action.defenderId, action.attackerId, ctx.dispatcher);
      if (instId.valid()) {
        result.appliedEffects.push_back(instId);
      }
    }

    return true;
  }

  /// Default event emission stage: emits combat events via the dispatcher.
  inline bool default_event_emission(CombatContext &ctx) {
    auto &action = ctx.action;
    auto &result = ctx.result;

    if (!ctx.dispatcher) return true;

    if (result.isMiss) {
      ctx.dispatcher->emit(combat_events::attack_missed,
                           SmallString{"combat.miss"},
                           action.attackerId, action.defenderId, 0.0);
      return true;
    }

    if (result.isBlocked) {
      ctx.dispatcher->emit(combat_events::attack_blocked,
                           SmallString{"combat.blocked"},
                           action.attackerId, action.defenderId,
                           result.finalDamage);
    }

    if (result.isCritical) {
      ctx.dispatcher->emit(combat_events::attack_critical,
                           SmallString{"combat.critical"},
                           action.attackerId, action.defenderId,
                           result.finalDamage);
    }

    if (action.is_healing()) {
      ctx.dispatcher->emit(combat_events::healing_dealt,
                           SmallString{"combat.heal_dealt"},
                           action.attackerId, action.defenderId,
                           result.finalHealing);
      ctx.dispatcher->emit(combat_events::healing_received,
                           SmallString{"combat.heal_recv"},
                           action.defenderId, action.attackerId,
                           result.finalHealing);
    } else {
      ctx.dispatcher->emit(combat_events::damage_dealt,
                           SmallString{"combat.dmg_dealt"},
                           action.attackerId, action.defenderId,
                           result.finalDamage);
      ctx.dispatcher->emit(combat_events::damage_received,
                           SmallString{"combat.dmg_recv"},
                           action.defenderId, action.attackerId,
                           result.finalDamage);
    }

    for (const auto &effectId : result.appliedEffects) {
      (void)effectId;  // ID already recorded in result
      ctx.dispatcher->emit(combat_events::effect_applied_in_combat,
                           SmallString{"combat.effect"},
                           action.attackerId, action.defenderId, 0.0);
    }

    return true;
  }

  // ---- Combat pipeline ----

  /// The combat resolution pipeline processes combat actions through a
  /// configurable sequence of stages.
  ///
  /// Usage:
  ///   1. Create a CombatPipeline and register stages (or use defaults)
  ///   2. Set system references (EffectSystem, EventDispatcher)
  ///   3. Call resolve() with a CombatAction
  ///   4. Inspect the returned CombatResult
  ///
  /// The pipeline supports:
  ///   - Configurable stages with priority ordering within each stage
  ///   - Custom stage handlers that can override or extend default behavior
  ///   - Deterministic testing via deterministicRoll
  ///   - Integration with EffectSystem for on-hit status effects
  ///   - Integration with StatBlock for stat-based calculations
  ///   - Event emission for telemetry and UI
  class CombatPipeline {
  public:
    CombatPipeline() = default;

    /// Install all default stage handlers.
    void install_defaults() {
      register_stage(CombatStage::pre_calculation,
                     SmallString{"default_pre_calc"},
                     default_pre_calculation, 0);
      register_stage(CombatStage::damage_calculation,
                     SmallString{"default_dmg_calc"},
                     default_damage_calculation, 0);
      register_stage(CombatStage::mitigation,
                     SmallString{"default_mitigation"},
                     default_mitigation, 0);
      register_stage(CombatStage::post_calculation,
                     SmallString{"default_post_calc"},
                     default_post_calculation, 0);
      register_stage(CombatStage::effect_application,
                     SmallString{"default_effect_app"},
                     default_effect_application, 0);
      register_stage(CombatStage::event_emission,
                     SmallString{"default_evt_emit"},
                     default_event_emission, 0);
    }

    /// Register a stage handler. Multiple handlers can be registered for
    /// the same stage; they execute in priority order (lower first).
    void register_stage(CombatStage stage, const SmallString &name,
                        CombatStageFn handler, i64 priority = 0) {
      CombatStageEntry entry{};
      entry.stage = stage;
      entry.name = name;
      entry.handler = static_cast<CombatStageFn &&>(handler);
      entry.priority = priority;
      _stages.push_back(static_cast<CombatStageEntry &&>(entry));
      _sorted = false;
    }

    /// Remove all handlers for a specific stage.
    size_t remove_stage(CombatStage stage) {
      size_t removed = 0;
      auto it = _stages.begin();
      while (it != _stages.end()) {
        if (it->stage == stage) {
          it = _stages.erase(it);
          ++removed;
        } else {
          ++it;
        }
      }
      return removed;
    }

    /// Remove a handler by name.
    bool remove_stage_by_name(const SmallString &name) {
      auto it = _stages.begin();
      while (it != _stages.end()) {
        if (it->name == name) {
          _stages.erase(it);
          return true;
        }
        ++it;
      }
      return false;
    }

    /// Replace a stage handler by name. Returns false if not found.
    bool replace_stage(const SmallString &name, CombatStageFn handler) {
      for (auto &entry : _stages) {
        if (entry.name == name) {
          entry.handler = static_cast<CombatStageFn &&>(handler);
          return true;
        }
      }
      return false;
    }

    /// Get total registered stage handler count.
    size_t stage_count() const noexcept { return _stages.size(); }

    /// Clear all stage handlers.
    void clear_stages() {
      _stages.clear();
      _sorted = false;
    }

    // ---- System references ----

    void set_effect_system(EffectSystem *sys) noexcept { _effectSystem = sys; }
    void set_ability_system(AbilitySystem *sys) noexcept { _abilitySystem = sys; }
    void set_dispatcher(GameplayEventDispatcher *disp) noexcept { _dispatcher = disp; }

    /// Set a deterministic roll value for testing (< 0 disables).
    void set_deterministic_roll(double value) noexcept { _deterministicRoll = value; }

    // ---- Resolution ----

    /// Resolve a combat action through the pipeline. Returns the result.
    CombatResult resolve(const CombatAction &action) {
      ensure_sorted();

      CombatContext ctx{};
      ctx.action = action;
      ctx.effectSystem = _effectSystem;
      ctx.abilitySystem = _abilitySystem;
      ctx.dispatcher = _dispatcher;
      ctx.deterministicRoll = _deterministicRoll;

      // Copy source data to result
      ctx.result.attackerId = action.attackerId;
      ctx.result.defenderId = action.defenderId;
      ctx.result.damageType = action.damageType;
      ctx.result.actionName = action.actionName;

      // Look up stat blocks if effect system is available
      if (_effectSystem) {
        ctx.attackerStats = _effectSystem->find_stat_block(action.attackerId);
        ctx.defenderStats = _effectSystem->find_stat_block(action.defenderId);
      }

      // Execute stages in order
      static constexpr CombatStage stageOrder[] = {
          CombatStage::pre_calculation,
          CombatStage::damage_calculation,
          CombatStage::mitigation,
          CombatStage::post_calculation,
          CombatStage::effect_application,
          CombatStage::event_emission};

      for (auto stageType : stageOrder) {
        for (const auto &entry : _stages) {
          if (entry.stage != stageType) continue;
          if (!entry.handler) continue;
          bool proceed = entry.handler(ctx);
          if (!proceed) {
            // Stage aborted the pipeline
            return ctx.result;
          }
        }
      }

      return ctx.result;
    }

    /// Resolve multiple combat actions in batch. Returns results in the
    /// same order as the input actions.
    std::vector<CombatResult> resolve_batch(
        const std::vector<CombatAction> &actions) {
      std::vector<CombatResult> results{};
      results.reserve(actions.size());
      for (const auto &action : actions) {
        results.push_back(resolve(action));
      }
      return results;
    }

    // ---- Convenience factory ----

    /// Create a pipeline with all default stages installed.
    static CombatPipeline with_defaults() {
      CombatPipeline pipeline{};
      pipeline.install_defaults();
      return pipeline;
    }

  private:
    void ensure_sorted() {
      if (_sorted) return;
      // Sort by stage enum value first, then by priority within stage
      std::sort(_stages.begin(), _stages.end(),
                [](const CombatStageEntry &a, const CombatStageEntry &b) {
                  if (a.stage != b.stage)
                    return static_cast<u8>(a.stage) < static_cast<u8>(b.stage);
                  return a.priority < b.priority;
                });
      _sorted = true;
    }

    std::vector<CombatStageEntry> _stages{};
    bool _sorted{false};

    // System references
    EffectSystem *_effectSystem{nullptr};
    AbilitySystem *_abilitySystem{nullptr};
    GameplayEventDispatcher *_dispatcher{nullptr};
    double _deterministicRoll{-1.0};
  };

  // ---- Health tracking utility ----

  /// Simple per-entity health tracker for combat scenarios.
  ///
  /// This is a lightweight utility that tracks current and max HP for
  /// entities. It integrates with CombatResult to apply damage/healing.
  class HealthTracker {
  public:
    HealthTracker() = default;

    /// Set max HP for an entity (also sets current HP to max).
    void set_max_hp(GameplayEntityId entityId, double maxHp) {
      auto &hp = _health[entityId.value];
      hp.maxHp = maxHp;
      hp.currentHp = maxHp;
    }

    /// Get current HP for an entity (0 if not registered).
    double current_hp(GameplayEntityId entityId) const noexcept {
      auto it = _health.find(entityId.value);
      return it != _health.end() ? it->second.currentHp : 0.0;
    }

    /// Get max HP for an entity (0 if not registered).
    double max_hp(GameplayEntityId entityId) const noexcept {
      auto it = _health.find(entityId.value);
      return it != _health.end() ? it->second.maxHp : 0.0;
    }

    /// Check if an entity is alive (has > 0 HP).
    bool is_alive(GameplayEntityId entityId) const noexcept {
      return current_hp(entityId) > 0.0;
    }

    /// Apply a combat result to the health tracker. Returns the actual
    /// amount of damage dealt or healing done (clamped by current/max HP).
    double apply_result(const CombatResult &result) {
      if (result.damageType == DamageType::healing) {
        return apply_healing(result.defenderId, result.finalHealing);
      }
      return apply_damage(result.defenderId, result.finalDamage);
    }

    /// Apply raw damage to an entity. Returns actual damage dealt.
    double apply_damage(GameplayEntityId entityId, double amount) {
      auto it = _health.find(entityId.value);
      if (it == _health.end()) return 0.0;

      double actual = amount;
      if (actual > it->second.currentHp) {
        actual = it->second.currentHp;
      }
      it->second.currentHp -= actual;
      if (it->second.currentHp < 0.0) it->second.currentHp = 0.0;
      return actual;
    }

    /// Apply healing to an entity. Returns actual healing done.
    double apply_healing(GameplayEntityId entityId, double amount) {
      auto it = _health.find(entityId.value);
      if (it == _health.end()) return 0.0;

      double missing = it->second.maxHp - it->second.currentHp;
      double actual = amount;
      if (actual > missing) actual = missing;
      if (actual < 0.0) actual = 0.0;
      it->second.currentHp += actual;
      return actual;
    }

    /// Set current HP directly (clamped to [0, maxHp]).
    void set_current_hp(GameplayEntityId entityId, double hp) {
      auto it = _health.find(entityId.value);
      if (it == _health.end()) return;
      if (hp < 0.0) hp = 0.0;
      if (hp > it->second.maxHp) hp = it->second.maxHp;
      it->second.currentHp = hp;
    }

    /// Number of tracked entities.
    size_t entity_count() const noexcept { return _health.size(); }

    /// Clear all health data.
    void clear() { _health.clear(); }

  private:
    struct EntityHealth {
      double currentHp{0.0};
      double maxHp{0.0};
    };

    std::unordered_map<u64, EntityHealth> _health{};
  };

}  // namespace zs
