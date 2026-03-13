#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayEntity.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayMechanicsSchema.hpp"

namespace zs {

  // ---- Ability identification ----

  /// Strongly-typed ability descriptor ID.
  struct AbilityDescriptorId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const AbilityDescriptorId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const AbilityDescriptorId &o) const noexcept {
      return value != o.value;
    }
  };

  /// Strongly-typed ability instance ID (unique per entity).
  struct AbilityInstanceId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const AbilityInstanceId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const AbilityInstanceId &o) const noexcept {
      return value != o.value;
    }
  };

  // ---- Ability state machine ----

  /// States an ability instance can be in.
  enum class AbilityState : u8 {
    inactive,     ///< Not active, ready to be activated (if off cooldown)
    activating,   ///< Activation in progress (cast time / wind-up)
    active,       ///< Currently executing its effect
    cooldown,     ///< Finished execution, waiting for cooldown to expire
    blocked       ///< Cannot be activated (missing resources, silenced, etc.)
  };

  /// Returns a human-readable name for an ability state.
  inline const char *ability_state_name(AbilityState state) noexcept {
    switch (state) {
      case AbilityState::inactive: return "inactive";
      case AbilityState::activating: return "activating";
      case AbilityState::active: return "active";
      case AbilityState::cooldown: return "cooldown";
      case AbilityState::blocked: return "blocked";
      default: return "unknown";
    }
  }

  // ---- Ability targeting ----

  /// How an ability selects its targets.
  enum class AbilityTargetMode : u8 {
    self_only,      ///< Targets the caster only
    single_target,  ///< Requires one target entity
    area,           ///< Targets all entities in an area (radius)
    no_target       ///< No target required (passive, aura, etc.)
  };

  // ---- Ability descriptor (data-driven definition) ----

  /// Describes the static properties of an ability type.
  ///
  /// AbilityDescriptor is the template from which AbilityInstances are created.
  /// It can be populated manually or loaded from a MechanicsDefinition.
  struct AbilityDescriptor {
    AbilityDescriptorId id{};
    SmallString name{};                ///< Human-readable ability name
    SmallString category{};            ///< Ability category (e.g., "attack", "heal", "buff")
    GameplayTagContainer tags{};       ///< Tags for classification and queries

    // Cost
    SmallString costResource{};        ///< Resource type consumed (e.g., "mana", "stamina")
    double costAmount{0.0};            ///< Amount consumed per activation

    // Timing (in seconds)
    double castTime{0.0};              ///< Time in activating state before becoming active
    double duration{0.0};              ///< Time in active state (0 = instant)
    double cooldownTime{0.0};          ///< Cooldown after execution completes

    // Targeting
    AbilityTargetMode targetMode{AbilityTargetMode::no_target};
    double range{0.0};                 ///< Max range for single_target or area center
    double radius{0.0};               ///< Area radius (for area targeting)

    // Gameplay values
    double power{0.0};                 ///< Primary power value (damage, heal amount, etc.)

    // Charges
    u32 maxCharges{1};                 ///< Max charges available (1 = no charge system)
    double chargeRechargeTime{0.0};    ///< Time to regain one charge

    /// Create an AbilityDescriptor from a MechanicsDefinition.
    /// Fields are read by conventional names; missing fields use defaults.
    static AbilityDescriptor from_definition(const MechanicsDefinition &def,
                                              AbilityDescriptorId descriptorId) {
      AbilityDescriptor desc{};
      desc.id = descriptorId;
      desc.name = def.id;  // Use definition id as name by default
      desc.tags = def.tags;

      // Read string fields
      desc.name = def.string_field(SmallString{"name"}, def.id);
      desc.category = def.string_field(SmallString{"category"}, SmallString{});
      desc.costResource = def.string_field(SmallString{"cost_resource"}, SmallString{});

      // Read number fields
      desc.costAmount = def.number_field(SmallString{"cost_amount"}, 0.0);
      desc.castTime = def.number_field(SmallString{"cast_time"}, 0.0);
      desc.duration = def.number_field(SmallString{"duration"}, 0.0);
      desc.cooldownTime = def.number_field(SmallString{"cooldown"}, 0.0);
      desc.power = def.number_field(SmallString{"power"}, 0.0);
      desc.range = def.number_field(SmallString{"range"}, 0.0);
      desc.radius = def.number_field(SmallString{"radius"}, 0.0);
      desc.chargeRechargeTime = def.number_field(SmallString{"charge_recharge_time"}, 0.0);

      // Read integer fields
      auto maxChargesVal = def.integer_field(SmallString{"max_charges"}, 1);
      desc.maxCharges = maxChargesVal > 0 ? static_cast<u32>(maxChargesVal) : 1;

      // Read target mode from string
      auto targetStr = def.string_field(SmallString{"target_mode"}, SmallString{"no_target"});
      if (targetStr == SmallString{"self_only"}) desc.targetMode = AbilityTargetMode::self_only;
      else if (targetStr == SmallString{"single_target"}) desc.targetMode = AbilityTargetMode::single_target;
      else if (targetStr == SmallString{"area"}) desc.targetMode = AbilityTargetMode::area;
      else desc.targetMode = AbilityTargetMode::no_target;

      return desc;
    }
  };

  // ---- Ability instance (runtime state) ----

  /// Result of attempting to activate an ability.
  enum class AbilityActivationResult : u8 {
    success,            ///< Activation started (or completed for instant abilities)
    on_cooldown,        ///< Ability is still on cooldown
    no_charges,         ///< No charges available
    insufficient_cost,  ///< Not enough resources
    blocked,            ///< Ability is blocked (silenced, stunned, etc.)
    invalid_target,     ///< No valid target provided for targeting mode
    already_active      ///< Ability is already activating or active
  };

  /// Returns a human-readable name for an activation result.
  inline const char *ability_activation_result_name(AbilityActivationResult r) noexcept {
    switch (r) {
      case AbilityActivationResult::success: return "success";
      case AbilityActivationResult::on_cooldown: return "on_cooldown";
      case AbilityActivationResult::no_charges: return "no_charges";
      case AbilityActivationResult::insufficient_cost: return "insufficient_cost";
      case AbilityActivationResult::blocked: return "blocked";
      case AbilityActivationResult::invalid_target: return "invalid_target";
      case AbilityActivationResult::already_active: return "already_active";
      default: return "unknown";
    }
  }

  /// Runtime state for a single ability instance owned by an entity.
  ///
  /// AbilityInstance tracks the current state machine position, timers,
  /// charges, and target for an ability granted to an entity.
  struct AbilityInstance {
    AbilityInstanceId instanceId{};
    AbilityDescriptorId descriptorId{};
    GameplayEntityId ownerId{};

    AbilityState state{AbilityState::inactive};

    // Timers (remaining seconds)
    double castTimeRemaining{0.0};
    double durationRemaining{0.0};
    double cooldownRemaining{0.0};

    // Charges
    u32 currentCharges{0};
    double chargeRechargeRemaining{0.0};

    // Target (for single_target abilities)
    GameplayEntityId targetId{};

    /// Whether the ability can potentially be activated (ignoring cost).
    bool can_activate() const noexcept {
      if (state == AbilityState::blocked) return false;
      if (state == AbilityState::activating || state == AbilityState::active) return false;
      if (state == AbilityState::cooldown && currentCharges == 0) return false;
      return currentCharges > 0;
    }

    /// Whether the ability is currently running (activating or active).
    bool is_running() const noexcept {
      return state == AbilityState::activating || state == AbilityState::active;
    }
  };

  // ---- Ability system ----

  /// Well-known event type IDs for ability events.
  namespace ability_events {
    constexpr GameplayEventTypeId activated{100};
    constexpr GameplayEventTypeId cast_completed{101};
    constexpr GameplayEventTypeId executed{102};
    constexpr GameplayEventTypeId cooldown_started{103};
    constexpr GameplayEventTypeId cooldown_finished{104};
    constexpr GameplayEventTypeId cancelled{105};
    constexpr GameplayEventTypeId blocked{106};
    constexpr GameplayEventTypeId charge_restored{107};
  }  // namespace ability_events

  /// Callback type for cost checking. Returns true if the entity has
  /// sufficient resources for the ability cost.
  using AbilityCostCheckFn = std::function<bool(GameplayEntityId owner,
                                                 const SmallString &resource,
                                                 double amount)>;

  /// Callback type for cost deduction. Called when ability activation
  /// consumes resources. Returns true on success.
  using AbilityCostDeductFn = std::function<bool(GameplayEntityId owner,
                                                  const SmallString &resource,
                                                  double amount)>;

  /// Callback type for ability execution. Called when an ability transitions
  /// to the active state (after cast time completes). This is where the
  /// actual gameplay effect happens (deal damage, apply heal, etc.).
  using AbilityExecuteFn = std::function<void(const AbilityInstance &instance,
                                               const AbilityDescriptor &descriptor)>;

  /// Manages ability descriptors, instances, and the activation/tick lifecycle.
  ///
  /// The AbilitySystem is the central manager for all abilities. It stores
  /// descriptors (ability templates), manages per-entity ability instances,
  /// and drives the state machine through tick updates.
  ///
  /// Usage pattern:
  ///   1. Register ability descriptors (or load from MechanicsDefinitions)
  ///   2. Grant abilities to entities
  ///   3. Call try_activate() to start abilities
  ///   4. Call tick() every frame with delta time
  ///   5. Handle events emitted during tick
  class AbilitySystem {
  public:
    AbilitySystem() = default;

    // ---- Descriptor management ----

    /// Register an ability descriptor. Returns false if the ID already exists.
    bool register_descriptor(const AbilityDescriptor &desc) {
      if (!desc.id.valid()) return false;
      auto [it, inserted] = _descriptors.emplace(desc.id.value, desc);
      return inserted;
    }

    /// Find a descriptor by ID.
    const AbilityDescriptor *find_descriptor(AbilityDescriptorId id) const noexcept {
      auto it = _descriptors.find(id.value);
      return it != _descriptors.end() ? &it->second : nullptr;
    }

    /// Remove a descriptor. Does NOT remove granted instances.
    bool remove_descriptor(AbilityDescriptorId id) {
      return _descriptors.erase(id.value) > 0;
    }

    size_t descriptor_count() const noexcept { return _descriptors.size(); }

    // ---- Instance management ----

    /// Grant an ability to an entity. Creates an instance from the descriptor.
    /// Returns the instance ID, or an invalid ID if the descriptor is not found
    /// or the entity already has this ability descriptor.
    AbilityInstanceId grant_ability(GameplayEntityId entityId,
                                     AbilityDescriptorId descriptorId) {
      auto *desc = find_descriptor(descriptorId);
      if (!desc) return AbilityInstanceId{0};

      // Check for duplicate grant
      auto &instances = _entityAbilities[entityId.value];
      for (const auto &inst : instances) {
        if (inst.descriptorId == descriptorId) return AbilityInstanceId{0};
      }

      AbilityInstance inst{};
      inst.instanceId = AbilityInstanceId{++_nextInstanceId};
      inst.descriptorId = descriptorId;
      inst.ownerId = entityId;
      inst.state = AbilityState::inactive;
      inst.currentCharges = desc->maxCharges;

      instances.push_back(inst);
      return inst.instanceId;
    }

    /// Revoke an ability instance from an entity. Returns true if found and removed.
    bool revoke_ability(GameplayEntityId entityId, AbilityInstanceId instanceId) {
      auto it = _entityAbilities.find(entityId.value);
      if (it == _entityAbilities.end()) return false;

      auto &instances = it->second;
      for (size_t i = 0; i < instances.size(); ++i) {
        if (instances[i].instanceId == instanceId) {
          instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(i));
          if (instances.empty()) _entityAbilities.erase(it);
          return true;
        }
      }
      return false;
    }

    /// Revoke all abilities from an entity.
    size_t revoke_all_abilities(GameplayEntityId entityId) {
      auto it = _entityAbilities.find(entityId.value);
      if (it == _entityAbilities.end()) return 0;
      size_t count = it->second.size();
      _entityAbilities.erase(it);
      return count;
    }

    /// Find an ability instance.
    const AbilityInstance *find_instance(GameplayEntityId entityId,
                                         AbilityInstanceId instanceId) const noexcept {
      auto it = _entityAbilities.find(entityId.value);
      if (it == _entityAbilities.end()) return nullptr;
      for (const auto &inst : it->second) {
        if (inst.instanceId == instanceId) return &inst;
      }
      return nullptr;
    }

    /// Find an ability instance by descriptor ID.
    const AbilityInstance *find_instance_by_descriptor(
        GameplayEntityId entityId,
        AbilityDescriptorId descriptorId) const noexcept {
      auto it = _entityAbilities.find(entityId.value);
      if (it == _entityAbilities.end()) return nullptr;
      for (const auto &inst : it->second) {
        if (inst.descriptorId == descriptorId) return &inst;
      }
      return nullptr;
    }

    /// Get all ability instances for an entity.
    std::vector<const AbilityInstance *> entity_abilities(
        GameplayEntityId entityId) const {
      std::vector<const AbilityInstance *> result{};
      auto it = _entityAbilities.find(entityId.value);
      if (it == _entityAbilities.end()) return result;
      result.reserve(it->second.size());
      for (const auto &inst : it->second) {
        result.push_back(&inst);
      }
      return result;
    }

    /// Get the total number of ability instances across all entities.
    size_t total_instance_count() const noexcept {
      size_t total = 0;
      for (const auto &entry : _entityAbilities) total += entry.second.size();
      return total;
    }

    // ---- Activation ----

    /// Try to activate an ability. Checks cost, cooldown, charges, and
    /// targeting requirements. On success, the ability enters the activating
    /// state (or active state if cast time is zero).
    AbilityActivationResult try_activate(GameplayEntityId entityId,
                                          AbilityInstanceId instanceId,
                                          GameplayEntityId targetId = {},
                                          GameplayEventDispatcher *dispatcher = nullptr) {
      auto *inst = find_instance_mut(entityId, instanceId);
      if (!inst) return AbilityActivationResult::blocked;

      auto *desc = find_descriptor(inst->descriptorId);
      if (!desc) return AbilityActivationResult::blocked;

      // Check state
      if (inst->state == AbilityState::blocked)
        return AbilityActivationResult::blocked;
      if (inst->is_running())
        return AbilityActivationResult::already_active;

      // Check charges
      if (inst->currentCharges == 0)
        return AbilityActivationResult::no_charges;

      // Check target
      if (desc->targetMode == AbilityTargetMode::single_target && !targetId.valid())
        return AbilityActivationResult::invalid_target;

      // Check cost
      if (desc->costAmount > 0.0 && desc->costResource.size() > 0) {
        if (_costCheck && !_costCheck(entityId, desc->costResource, desc->costAmount))
          return AbilityActivationResult::insufficient_cost;
      }

      // Deduct cost
      if (desc->costAmount > 0.0 && desc->costResource.size() > 0) {
        if (_costDeduct) {
          if (!_costDeduct(entityId, desc->costResource, desc->costAmount))
            return AbilityActivationResult::insufficient_cost;
        }
      }

      // Consume a charge
      if (inst->currentCharges > 0) --inst->currentCharges;

      // Start charge recharge if needed
      if (inst->currentCharges < desc->maxCharges && desc->chargeRechargeTime > 0.0) {
        inst->chargeRechargeRemaining = desc->chargeRechargeTime;
      }

      // Set target
      inst->targetId = targetId;

      // Transition to activating or active
      if (desc->castTime > 0.0) {
        inst->state = AbilityState::activating;
        inst->castTimeRemaining = desc->castTime;
        if (dispatcher) {
          dispatcher->emit(ability_events::activated, SmallString{"ability.activated"},
                           entityId, targetId, 0.0);
        }
      } else {
        // Instant cast: go directly to active
        enter_active_state(*inst, *desc, dispatcher);
      }

      return AbilityActivationResult::success;
    }

    /// Cancel a running ability. Returns true if the ability was running.
    bool cancel_ability(GameplayEntityId entityId, AbilityInstanceId instanceId,
                        GameplayEventDispatcher *dispatcher = nullptr) {
      auto *inst = find_instance_mut(entityId, instanceId);
      if (!inst) return false;
      if (!inst->is_running()) return false;

      auto *desc = find_descriptor(inst->descriptorId);

      // Enter cooldown or inactive
      if (desc && desc->cooldownTime > 0.0) {
        inst->state = AbilityState::cooldown;
        inst->cooldownRemaining = desc->cooldownTime;
        inst->castTimeRemaining = 0.0;
        inst->durationRemaining = 0.0;
      } else {
        inst->state = AbilityState::inactive;
        inst->castTimeRemaining = 0.0;
        inst->durationRemaining = 0.0;
        inst->cooldownRemaining = 0.0;
      }

      if (dispatcher) {
        dispatcher->emit(ability_events::cancelled, SmallString{"ability.cancelled"},
                         entityId, inst->targetId, 0.0);
      }
      return true;
    }

    /// Block an ability. While blocked, the ability cannot be activated.
    bool block_ability(GameplayEntityId entityId, AbilityInstanceId instanceId) {
      auto *inst = find_instance_mut(entityId, instanceId);
      if (!inst) return false;
      inst->state = AbilityState::blocked;
      inst->castTimeRemaining = 0.0;
      inst->durationRemaining = 0.0;
      return true;
    }

    /// Unblock an ability. Returns it to inactive state.
    bool unblock_ability(GameplayEntityId entityId, AbilityInstanceId instanceId) {
      auto *inst = find_instance_mut(entityId, instanceId);
      if (!inst) return false;
      if (inst->state != AbilityState::blocked) return false;
      inst->state = AbilityState::inactive;
      return true;
    }

    // ---- Tick update ----

    /// Advance all ability instances by deltaTime seconds.
    /// This drives the state machine: activating -> active -> cooldown -> inactive.
    void tick(double deltaTime, GameplayEventDispatcher *dispatcher = nullptr) {
      for (auto &entityEntry : _entityAbilities) {
        for (auto &inst : entityEntry.second) {
          tick_instance(inst, deltaTime, dispatcher);
        }
      }
    }

    // ---- Cost callbacks ----

    void set_cost_check(AbilityCostCheckFn fn) { _costCheck = static_cast<AbilityCostCheckFn &&>(fn); }
    void set_cost_deduct(AbilityCostDeductFn fn) { _costDeduct = static_cast<AbilityCostDeductFn &&>(fn); }
    void set_execute_callback(AbilityExecuteFn fn) { _executeFn = static_cast<AbilityExecuteFn &&>(fn); }

    // ---- Clear ----

    void clear() {
      _descriptors.clear();
      _entityAbilities.clear();
      _nextInstanceId = 0;
    }

  private:
    /// Get mutable instance pointer (private helper).
    AbilityInstance *find_instance_mut(GameplayEntityId entityId,
                                       AbilityInstanceId instanceId) noexcept {
      auto it = _entityAbilities.find(entityId.value);
      if (it == _entityAbilities.end()) return nullptr;
      for (auto &inst : it->second) {
        if (inst.instanceId == instanceId) return &inst;
      }
      return nullptr;
    }

    /// Transition to active state and invoke execution callback.
    void enter_active_state(AbilityInstance &inst, const AbilityDescriptor &desc,
                            GameplayEventDispatcher *dispatcher) {
      inst.state = AbilityState::active;
      inst.castTimeRemaining = 0.0;

      if (desc.duration > 0.0) {
        inst.durationRemaining = desc.duration;
      } else {
        // Instant ability: execute immediately and go to cooldown
        inst.durationRemaining = 0.0;
      }

      if (dispatcher) {
        dispatcher->emit(ability_events::cast_completed, SmallString{"ability.cast_done"},
                         inst.ownerId, inst.targetId, desc.power);
      }

      // Invoke execution callback
      if (_executeFn) {
        _executeFn(inst, desc);
      }

      if (dispatcher) {
        dispatcher->emit(ability_events::executed, SmallString{"ability.executed"},
                         inst.ownerId, inst.targetId, desc.power);
      }

      // For instant abilities (duration == 0), go to cooldown immediately
      if (desc.duration <= 0.0) {
        enter_cooldown_or_inactive(inst, desc, dispatcher);
      }
    }

    /// Transition to cooldown or directly to inactive.
    void enter_cooldown_or_inactive(AbilityInstance &inst, const AbilityDescriptor &desc,
                                     GameplayEventDispatcher *dispatcher) {
      if (desc.cooldownTime > 0.0) {
        inst.state = AbilityState::cooldown;
        inst.cooldownRemaining = desc.cooldownTime;
        if (dispatcher) {
          dispatcher->emit(ability_events::cooldown_started,
                           SmallString{"ability.cd_start"},
                           inst.ownerId, inst.targetId, desc.cooldownTime);
        }
      } else {
        inst.state = AbilityState::inactive;
        inst.cooldownRemaining = 0.0;
      }
    }

    /// Tick a single ability instance.
    void tick_instance(AbilityInstance &inst, double dt,
                       GameplayEventDispatcher *dispatcher) {
      auto *desc = find_descriptor(inst.descriptorId);
      if (!desc) return;

      switch (inst.state) {
        case AbilityState::activating: {
          inst.castTimeRemaining -= dt;
          if (inst.castTimeRemaining <= 0.0) {
            inst.castTimeRemaining = 0.0;
            enter_active_state(inst, *desc, dispatcher);
          }
          break;
        }

        case AbilityState::active: {
          if (desc->duration > 0.0) {
            inst.durationRemaining -= dt;
            if (inst.durationRemaining <= 0.0) {
              inst.durationRemaining = 0.0;
              enter_cooldown_or_inactive(inst, *desc, dispatcher);
            }
          }
          // For instant abilities (duration==0), we already transitioned in enter_active_state
          break;
        }

        case AbilityState::cooldown: {
          inst.cooldownRemaining -= dt;
          if (inst.cooldownRemaining <= 0.0) {
            inst.cooldownRemaining = 0.0;
            inst.state = AbilityState::inactive;
            if (dispatcher) {
              dispatcher->emit(ability_events::cooldown_finished,
                               SmallString{"ability.cd_done"},
                               inst.ownerId, {}, 0.0);
            }
          }
          break;
        }

        default:
          break;
      }

      // Charge recharge (runs regardless of state)
      if (inst.currentCharges < desc->maxCharges && desc->chargeRechargeTime > 0.0) {
        inst.chargeRechargeRemaining -= dt;
        while (inst.chargeRechargeRemaining <= 0.0
               && inst.currentCharges < desc->maxCharges) {
          ++inst.currentCharges;
          if (dispatcher) {
            dispatcher->emit(ability_events::charge_restored,
                             SmallString{"ability.charge"},
                             inst.ownerId, {},
                             static_cast<double>(inst.currentCharges));
          }
          if (inst.currentCharges < desc->maxCharges) {
            inst.chargeRechargeRemaining += desc->chargeRechargeTime;
          } else {
            inst.chargeRechargeRemaining = 0.0;
          }
        }
      }
    }

    std::unordered_map<u64, AbilityDescriptor> _descriptors{};
    std::unordered_map<u64, std::vector<AbilityInstance>> _entityAbilities{};
    u64 _nextInstanceId{0};

    AbilityCostCheckFn _costCheck{};
    AbilityCostDeductFn _costDeduct{};
    AbilityExecuteFn _executeFn{};
  };

}  // namespace zs
