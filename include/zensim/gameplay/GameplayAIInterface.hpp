#pragma once

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayAbility.hpp"
#include "zensim/gameplay/GameplayCombat.hpp"
#include "zensim/gameplay/GameplayEffect.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"

namespace zs {

  // =====================================================================
  //  AI Entity Snapshot — read-only state view for AI decision-making
  // =====================================================================

  /// Lightweight snapshot of an ability's state for AI inspection.
  struct AIAbilityInfo {
    AbilityDescriptorId descriptorId;
    AbilityInstanceId instanceId;
    SmallString name;
    SmallString category;
    AbilityState state{AbilityState::inactive};
    AbilityTargetMode targetMode{AbilityTargetMode::no_target};
    double power{0.0};
    double range{0.0};
    double radius{0.0};
    double cooldownRemaining{0.0};
    double costAmount{0.0};
    SmallString costResource;
    u32 currentCharges{0};
    u32 maxCharges{1};
    bool canActivate{false};
  };

  /// Lightweight snapshot of an active effect for AI inspection.
  struct AIEffectInfo {
    EffectDescriptorId descriptorId;
    EffectInstanceId instanceId;
    SmallString name;
    EffectDurationType durationType{EffectDurationType::instant};
    double remainingDuration{0.0};
    u32 stackCount{0};
    bool isBuff{false};   ///< Hint: positive effect
    bool isDebuff{false}; ///< Hint: negative effect
  };

  /// Read-only snapshot of an entity's gameplay state.
  struct AIEntitySnapshot {
    GameplayEntityId entityId;
    double currentHp{0.0};
    double maxHp{0.0};
    double hpFraction{0.0};  ///< currentHp / maxHp, [0, 1]
    bool alive{true};

    std::vector<AIAbilityInfo> abilities;
    std::vector<AIEffectInfo> activeEffects;
    std::vector<std::pair<SmallString, double>> stats;  ///< (name, computed value)

    /// Check if entity has an ability that can be activated.
    bool has_activatable_ability() const noexcept {
      for (auto &a : abilities) {
        if (a.canActivate) return true;
      }
      return false;
    }

    /// Find an ability info by descriptor ID.
    const AIAbilityInfo *find_ability(AbilityDescriptorId id) const noexcept {
      for (auto &a : abilities) {
        if (a.descriptorId == id) return &a;
      }
      return nullptr;
    }

    /// Get computed stat value by name. Returns 0 if not found.
    double stat(const SmallString &name) const noexcept {
      for (auto &s : stats) {
        if (s.first == name) return s.second;
      }
      return 0.0;
    }

    /// Check if the entity has an active effect from a given descriptor.
    bool has_effect(EffectDescriptorId id) const noexcept {
      for (auto &e : activeEffects) {
        if (e.descriptorId == id) return true;
      }
      return false;
    }
  };

  // =====================================================================
  //  AI Action Types
  // =====================================================================

  /// Kind of action an AI can take.
  enum class AIActionKind : u8 {
    use_ability,    ///< Activate an ability on a target
    move_to,        ///< Move to a position (placeholder, no position system yet)
    wait,           ///< Do nothing this turn
    flee,           ///< Attempt to disengage
    use_item        ///< Use an inventory item (placeholder)
  };

  inline const char *ai_action_kind_name(AIActionKind k) noexcept {
    switch (k) {
      case AIActionKind::use_ability: return "use_ability";
      case AIActionKind::move_to: return "move_to";
      case AIActionKind::wait: return "wait";
      case AIActionKind::flee: return "flee";
      case AIActionKind::use_item: return "use_item";
      default: return "unknown";
    }
  }

  /// Feasibility assessment for an action.
  enum class AIActionFeasibility : u8 {
    feasible,           ///< Action can be performed right now
    on_cooldown,        ///< Ability is on cooldown
    no_charges,         ///< No charges remaining
    insufficient_cost,  ///< Not enough resources
    out_of_range,       ///< Target is too far (requires external range check)
    invalid_target,     ///< No valid target
    blocked,            ///< Entity is blocked from acting
    not_available       ///< Action is not available at all
  };

  inline const char *ai_action_feasibility_name(AIActionFeasibility f) noexcept {
    switch (f) {
      case AIActionFeasibility::feasible: return "feasible";
      case AIActionFeasibility::on_cooldown: return "on_cooldown";
      case AIActionFeasibility::no_charges: return "no_charges";
      case AIActionFeasibility::insufficient_cost: return "insufficient_cost";
      case AIActionFeasibility::out_of_range: return "out_of_range";
      case AIActionFeasibility::invalid_target: return "invalid_target";
      case AIActionFeasibility::blocked: return "blocked";
      case AIActionFeasibility::not_available: return "not_available";
      default: return "unknown";
    }
  }

  /// A candidate action that AI can evaluate and choose from.
  struct AIActionCandidate {
    AIActionKind kind{AIActionKind::wait};
    AbilityDescriptorId abilityId;       ///< For use_ability actions
    GameplayEntityId targetEntity;       ///< Target entity (if applicable)
    AIActionFeasibility feasibility{AIActionFeasibility::not_available};
    double score{0.0};                   ///< AI-computed desirability score
    SmallString label;                   ///< Human-readable label for debug

    bool is_feasible() const noexcept {
      return feasibility == AIActionFeasibility::feasible;
    }
  };

  /// Request from AI to perform an action.
  struct AIActionRequest {
    GameplayEntityId actorId;
    AIActionKind kind{AIActionKind::wait};
    AbilityDescriptorId abilityId;
    GameplayEntityId targetId;
  };

  /// Result of executing an AI action request.
  struct AIActionResult {
    bool success{false};
    SmallString reason;                           ///< Failure reason if !success
    AbilityActivationResult activationResult{AbilityActivationResult::success};
  };

  // =====================================================================
  //  AI Debug Record
  // =====================================================================

  /// A single decision record for AI debugging/inspection.
  struct AIDebugRecord {
    u64 tick{0};                                 ///< Simulation tick
    GameplayEntityId actorId;
    std::vector<AIActionCandidate> candidates;   ///< All evaluated candidates
    AIActionCandidate chosenAction;              ///< The selected action
    SmallString reasoning;                       ///< Optional explanation

    /// Format as JSON string for logging/inspection.
    std::string to_json() const {
      std::ostringstream os;
      os << "{\"tick\":" << tick
         << ",\"actor\":" << actorId.value
         << ",\"chosen\":{\"kind\":\"" << ai_action_kind_name(chosenAction.kind)
         << "\",\"score\":" << chosenAction.score
         << ",\"label\":\"" << chosenAction.label.asChars()
         << "\",\"feasibility\":\"" << ai_action_feasibility_name(chosenAction.feasibility)
         << "\"},\"candidates\":[";
      for (size_t i = 0; i < candidates.size(); ++i) {
        if (i > 0) os << ",";
        os << "{\"kind\":\"" << ai_action_kind_name(candidates[i].kind)
           << "\",\"score\":" << candidates[i].score
           << ",\"label\":\"" << candidates[i].label.asChars()
           << "\",\"feasible\":" << (candidates[i].is_feasible() ? "true" : "false")
           << "}";
      }
      os << "],\"reasoning\":\"" << reasoning.asChars() << "\"}";
      return os.str();
    }
  };

  // =====================================================================
  //  AI Score Function Hook
  // =====================================================================

  /// User-provided scoring function.
  /// Arguments: (actor snapshot, target snapshot, action candidate)
  /// Returns: score (higher = more desirable)
  using AIScoreFunction = std::function<double(
      const AIEntitySnapshot &actor,
      const AIEntitySnapshot &target,
      const AIActionCandidate &candidate)>;

  // =====================================================================
  //  AI World View — central query interface
  // =====================================================================

  /// Central AI interface that aggregates gameplay subsystems and provides
  /// a unified query/action layer for AI consumers.
  ///
  /// Usage pattern:
  ///   1. Attach subsystem pointers (ability, effect, combat, health)
  ///   2. Call snapshot() to get entity state
  ///   3. Call enumerate_actions() to get scored candidates
  ///   4. Pick the best candidate
  ///   5. Call execute_action() to perform it
  ///   6. Debug records are stored for inspection
  class AIWorldView {
  public:
    // ---- Subsystem attachment ----

    void set_ability_system(AbilitySystem *sys) noexcept { abilitySys_ = sys; }
    void set_effect_system(EffectSystem *sys) noexcept { effectSys_ = sys; }
    void set_combat_pipeline(CombatPipeline *pipeline) noexcept { combatPipeline_ = pipeline; }
    void set_health_tracker(HealthTracker *tracker) noexcept { healthTracker_ = tracker; }
    void set_dispatcher(GameplayEventDispatcher *disp) noexcept { dispatcher_ = disp; }

    AbilitySystem *ability_system() const noexcept { return abilitySys_; }
    EffectSystem *effect_system() const noexcept { return effectSys_; }
    CombatPipeline *combat_pipeline() const noexcept { return combatPipeline_; }
    HealthTracker *health_tracker() const noexcept { return healthTracker_; }

    // ---- Scoring ----

    /// Set the scoring function used to evaluate action candidates.
    void set_score_function(AIScoreFunction fn) {
      scoreFn_ = std::move(fn);
    }

    // ---- Entity management ----

    /// Register an entity as known to the AI world. Entities must be
    /// registered to be included in target enumeration.
    void register_entity(GameplayEntityId entityId) {
      for (auto &e : entities_) {
        if (e == entityId) return;
      }
      entities_.push_back(entityId);
    }

    /// Remove an entity from the AI world.
    void unregister_entity(GameplayEntityId entityId) {
      entities_.erase(
        std::remove(entities_.begin(), entities_.end(), entityId),
        entities_.end());
    }

    /// Get all registered entity IDs.
    const std::vector<GameplayEntityId> &entities() const noexcept {
      return entities_;
    }

    /// Number of registered entities.
    u32 entity_count() const noexcept {
      return static_cast<u32>(entities_.size());
    }

    // ---- Snapshot Generation ----

    /// Generate a read-only snapshot of an entity's current state.
    AIEntitySnapshot snapshot(GameplayEntityId entityId) const {
      AIEntitySnapshot snap;
      snap.entityId = entityId;

      // Health
      if (healthTracker_) {
        snap.currentHp = healthTracker_->current_hp(entityId);
        snap.maxHp = healthTracker_->max_hp(entityId);
        snap.alive = healthTracker_->is_alive(entityId);
        snap.hpFraction = (snap.maxHp > 0.0)
          ? snap.currentHp / snap.maxHp : 0.0;
      }

      // Abilities
      if (abilitySys_) {
        auto instances = abilitySys_->entity_abilities(entityId);
        snap.abilities.reserve(instances.size());
        for (auto *inst : instances) {
          AIAbilityInfo info;
          info.descriptorId = inst->descriptorId;
          info.instanceId = inst->instanceId;
          info.state = inst->state;
          info.cooldownRemaining = inst->cooldownRemaining;
          info.currentCharges = inst->currentCharges;
          info.canActivate = inst->can_activate();

          auto *desc = abilitySys_->find_descriptor(inst->descriptorId);
          if (desc) {
            info.name = desc->name;
            info.category = desc->category;
            info.targetMode = desc->targetMode;
            info.power = desc->power;
            info.range = desc->range;
            info.radius = desc->radius;
            info.costAmount = desc->costAmount;
            info.costResource = desc->costResource;
            info.maxCharges = desc->maxCharges;
          }

          snap.abilities.push_back(info);
        }
      }

      // Active effects
      if (effectSys_) {
        auto effects = effectSys_->entity_effects(entityId);
        snap.activeEffects.reserve(effects.size());
        for (auto *inst : effects) {
          AIEffectInfo info;
          info.instanceId = inst->instanceId;
          info.descriptorId = inst->descriptorId;
          info.stackCount = inst->stackCount;
          info.remainingDuration = inst->durationRemaining;

          auto *desc = effectSys_->find_descriptor(inst->descriptorId);
          if (desc) {
            info.name = desc->name;
            info.durationType = desc->durationType;
          }
          snap.activeEffects.push_back(info);
        }

        // Stats
        auto *statBlock = effectSys_->find_stat_block(entityId);
        if (statBlock) {
          auto statNames = statBlock->stat_names();
          snap.stats.reserve(statNames.size());
          for (auto &name : statNames) {
            snap.stats.push_back({name, statBlock->compute(name)});
          }
        }
      }

      return snap;
    }

    // ---- Action Enumeration ----

    /// Enumerate all possible actions for an entity, with feasibility checks
    /// and scoring. Actions include using each ability on each valid target,
    /// plus the "wait" action.
    std::vector<AIActionCandidate> enumerate_actions(
        GameplayEntityId actorId) const {
      std::vector<AIActionCandidate> candidates;

      AIEntitySnapshot actorSnap = snapshot(actorId);

      // Always include "wait" as a baseline action
      {
        AIActionCandidate wait;
        wait.kind = AIActionKind::wait;
        wait.feasibility = AIActionFeasibility::feasible;
        wait.label = SmallString{"Wait"};
        wait.score = 0.0;

        if (scoreFn_) {
          wait.score = scoreFn_(actorSnap, actorSnap, wait);
        }
        candidates.push_back(wait);
      }

      // Enumerate ability actions
      if (abilitySys_) {
        for (auto &abilInfo : actorSnap.abilities) {
          if (abilInfo.targetMode == AbilityTargetMode::no_target
              || abilInfo.targetMode == AbilityTargetMode::self_only) {
            // Self-target or no-target: one candidate per ability
            AIActionCandidate c;
            c.kind = AIActionKind::use_ability;
            c.abilityId = abilInfo.descriptorId;
            c.targetEntity = actorId;
            c.label = abilInfo.name;

            c.feasibility = assess_feasibility(abilInfo);

            if (scoreFn_) {
              c.score = scoreFn_(actorSnap, actorSnap, c);
            }
            candidates.push_back(c);
          } else {
            // Targeted ability: one candidate per potential target
            for (auto &targetId : entities_) {
              if (targetId == actorId) continue;  // Skip self for offensive abilities

              AIActionCandidate c;
              c.kind = AIActionKind::use_ability;
              c.abilityId = abilInfo.descriptorId;
              c.targetEntity = targetId;
              c.label = abilInfo.name;

              c.feasibility = assess_feasibility(abilInfo);

              if (scoreFn_) {
                AIEntitySnapshot targetSnap = snapshot(targetId);
                c.score = scoreFn_(actorSnap, targetSnap, c);
              }
              candidates.push_back(c);
            }
          }
        }
      }

      // Sort by score (descending)
      std::sort(candidates.begin(), candidates.end(),
        [](const AIActionCandidate &a, const AIActionCandidate &b) {
          return a.score > b.score;
        });

      return candidates;
    }

    /// Get the best (highest-scored) feasible action.
    /// Returns a wait action if no feasible actions exist.
    AIActionCandidate best_action(GameplayEntityId actorId) const {
      auto candidates = enumerate_actions(actorId);
      for (auto &c : candidates) {
        if (c.is_feasible()) return c;
      }
      // Fallback: wait
      AIActionCandidate wait;
      wait.kind = AIActionKind::wait;
      wait.feasibility = AIActionFeasibility::feasible;
      wait.label = SmallString{"Wait"};
      wait.score = 0.0;
      return wait;
    }

    // ---- Action Execution ----

    /// Execute an action request through the appropriate subsystem.
    AIActionResult execute_action(const AIActionRequest &request) {
      AIActionResult result;

      switch (request.kind) {
        case AIActionKind::use_ability: {
          if (!abilitySys_) {
            result.success = false;
            result.reason = SmallString{"no_ability_system"};
            return result;
          }

          // Find the ability instance by descriptor
          auto *inst = abilitySys_->find_instance_by_descriptor(
            request.actorId, request.abilityId);
          if (!inst) {
            result.success = false;
            result.reason = SmallString{"ability_not_found"};
            return result;
          }

          auto activResult = abilitySys_->try_activate(
            request.actorId, inst->instanceId, request.targetId, dispatcher_);
          result.activationResult = activResult;
          result.success = (activResult == AbilityActivationResult::success);
          if (!result.success) {
            result.reason = SmallString{ability_activation_result_name(activResult)};
          }
          break;
        }
        case AIActionKind::wait:
          result.success = true;
          break;
        default:
          result.success = false;
          result.reason = SmallString{"unsupported_action"};
          break;
      }

      return result;
    }

    // ---- Decision Recording ----

    /// Record a decision for debug/inspection.
    void record_decision(const AIDebugRecord &record) {
      if (debugHistory_.size() >= debugCapacity_) {
        debugHistory_.erase(debugHistory_.begin());
      }
      debugHistory_.push_back(record);
    }

    /// Create and record a full decision cycle: enumerate, score, choose best.
    AIActionCandidate decide_and_record(GameplayEntityId actorId, u64 tick,
                                        const SmallString &reasoning = SmallString{}) {
      auto candidates = enumerate_actions(actorId);
      AIActionCandidate chosen;
      chosen.kind = AIActionKind::wait;
      chosen.feasibility = AIActionFeasibility::feasible;
      chosen.label = SmallString{"Wait"};

      for (auto &c : candidates) {
        if (c.is_feasible()) {
          chosen = c;
          break;
        }
      }

      AIDebugRecord record;
      record.tick = tick;
      record.actorId = actorId;
      record.candidates = candidates;
      record.chosenAction = chosen;
      record.reasoning = reasoning;
      record_decision(record);

      return chosen;
    }

    /// Get the debug decision history.
    const std::vector<AIDebugRecord> &debug_history() const noexcept {
      return debugHistory_;
    }

    /// Set maximum debug history capacity.
    void set_debug_capacity(u32 capacity) {
      debugCapacity_ = capacity;
    }

    /// Clear debug history.
    void clear_debug_history() {
      debugHistory_.clear();
    }

    /// Export debug history as a JSON array string.
    std::string export_debug_json() const {
      std::ostringstream os;
      os << "[";
      for (size_t i = 0; i < debugHistory_.size(); ++i) {
        if (i > 0) os << ",";
        os << debugHistory_[i].to_json();
      }
      os << "]";
      return os.str();
    }

    // ---- Utility ----

    /// Clear all state (entities, debug history).
    void clear() {
      entities_.clear();
      debugHistory_.clear();
      abilitySys_ = nullptr;
      effectSys_ = nullptr;
      combatPipeline_ = nullptr;
      healthTracker_ = nullptr;
      dispatcher_ = nullptr;
      scoreFn_ = {};
    }

    /// Clear entity registrations only.
    void clear_entities() {
      entities_.clear();
    }

  private:
    /// Assess feasibility of an ability action from its snapshot info.
    static AIActionFeasibility assess_feasibility(const AIAbilityInfo &info) noexcept {
      if (info.state == AbilityState::blocked)
        return AIActionFeasibility::blocked;
      if (info.state == AbilityState::cooldown)
        return AIActionFeasibility::on_cooldown;
      if (info.state == AbilityState::activating || info.state == AbilityState::active)
        return AIActionFeasibility::blocked;
      if (info.currentCharges == 0 && info.maxCharges > 1)
        return AIActionFeasibility::no_charges;
      if (!info.canActivate)
        return AIActionFeasibility::not_available;
      return AIActionFeasibility::feasible;
    }

    // ---- State ----
    std::vector<GameplayEntityId> entities_;
    AbilitySystem *abilitySys_{nullptr};
    EffectSystem *effectSys_{nullptr};
    CombatPipeline *combatPipeline_{nullptr};
    HealthTracker *healthTracker_{nullptr};
    GameplayEventDispatcher *dispatcher_{nullptr};
    AIScoreFunction scoreFn_;
    std::vector<AIDebugRecord> debugHistory_;
    u32 debugCapacity_{1000};
  };

}  // namespace zs
