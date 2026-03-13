#pragma once

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayMechanicsSchema.hpp"

namespace zs {

  // ---- Effect identification ----

  /// Strongly-typed effect descriptor ID.
  struct EffectDescriptorId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const EffectDescriptorId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const EffectDescriptorId &o) const noexcept {
      return value != o.value;
    }
  };

  /// Strongly-typed effect instance ID (unique globally).
  struct EffectInstanceId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const EffectInstanceId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const EffectInstanceId &o) const noexcept {
      return value != o.value;
    }
  };

  // ---- Effect types ----

  /// How an effect behaves over time.
  enum class EffectDurationType : u8 {
    instant,    ///< Applied once immediately, no ongoing state
    duration,   ///< Active for a fixed period, then removed
    periodic,   ///< Applies its payload repeatedly at intervals
    infinite    ///< Active until explicitly removed
  };

  inline const char *effect_duration_type_name(EffectDurationType t) noexcept {
    switch (t) {
      case EffectDurationType::instant: return "instant";
      case EffectDurationType::duration: return "duration";
      case EffectDurationType::periodic: return "periodic";
      case EffectDurationType::infinite: return "infinite";
      default: return "unknown";
    }
  }

  // ---- Stacking policy ----

  /// How multiple instances of the same effect combine.
  enum class EffectStackPolicy : u8 {
    replace,        ///< New application replaces existing (resets timer)
    stack_count,    ///< Increases stack count (up to max)
    stack_duration, ///< Extends duration of existing instance
    independent     ///< Each application is a separate instance
  };

  // ---- Stat modification ----

  /// The operation a stat modifier applies in the modification chain.
  ///
  /// Modifiers are evaluated in this order:
  ///   1. base_override (if any, replaces base value)
  ///   2. additive (added to base)
  ///   3. multiplicative (multiplied with running total)
  ///   4. final_override (if any, replaces everything)
  enum class StatModOp : u8 {
    additive,       ///< Add value to stat
    multiplicative, ///< Multiply stat by value
    base_override,  ///< Replace base value entirely
    final_override  ///< Replace final computed value entirely
  };

  inline const char *stat_mod_op_name(StatModOp op) noexcept {
    switch (op) {
      case StatModOp::additive: return "additive";
      case StatModOp::multiplicative: return "multiplicative";
      case StatModOp::base_override: return "base_override";
      case StatModOp::final_override: return "final_override";
      default: return "unknown";
    }
  }

  /// A single stat modification entry.
  struct StatModifier {
    SmallString statName{};       ///< Which stat to modify (e.g., "strength", "max_hp")
    StatModOp operation{StatModOp::additive};
    double value{0.0};            ///< Modifier value
    EffectInstanceId sourceEffect{}; ///< Which effect instance applied this modifier
    i64 priority{0};              ///< Higher priority modifiers are applied later
  };

  // ---- Effect descriptor (data-driven) ----

  /// Describes the static properties of an effect type.
  struct EffectDescriptor {
    EffectDescriptorId id{};
    SmallString name{};
    GameplayTagContainer tags{};

    EffectDurationType durationType{EffectDurationType::instant};
    double duration{0.0};         ///< Seconds (for duration/periodic types)
    double period{0.0};           ///< Seconds between ticks (for periodic type)
    double magnitude{0.0};        ///< Primary effect magnitude

    // Stacking
    EffectStackPolicy stackPolicy{EffectStackPolicy::replace};
    u32 maxStacks{1};             ///< Maximum stack count

    // Stat modifiers applied while this effect is active
    std::vector<StatModifier> modifiers{};

    // Tags that grant immunity to this effect
    GameplayTagContainer immunityTags{};

    // Tags that this effect cancels on the target when applied
    GameplayTagContainer cancellationTags{};

    // Tags granted to the target while this effect is active
    GameplayTagContainer grantedTags{};

    /// Create an EffectDescriptor from a MechanicsDefinition.
    static EffectDescriptor from_definition(const MechanicsDefinition &def,
                                             EffectDescriptorId descriptorId) {
      EffectDescriptor desc{};
      desc.id = descriptorId;
      desc.name = def.string_field(SmallString{"name"}, def.id);
      desc.tags = def.tags;

      desc.duration = def.number_field(SmallString{"duration"}, 0.0);
      desc.period = def.number_field(SmallString{"period"}, 0.0);
      desc.magnitude = def.number_field(SmallString{"magnitude"}, 0.0);
      desc.maxStacks = static_cast<u32>(
          def.integer_field(SmallString{"max_stacks"}, 1));

      auto dtStr = def.string_field(SmallString{"duration_type"}, SmallString{"instant"});
      if (dtStr == SmallString{"duration"}) desc.durationType = EffectDurationType::duration;
      else if (dtStr == SmallString{"periodic"}) desc.durationType = EffectDurationType::periodic;
      else if (dtStr == SmallString{"infinite"}) desc.durationType = EffectDurationType::infinite;
      else desc.durationType = EffectDurationType::instant;

      auto spStr = def.string_field(SmallString{"stack_policy"}, SmallString{"replace"});
      if (spStr == SmallString{"stack_count"}) desc.stackPolicy = EffectStackPolicy::stack_count;
      else if (spStr == SmallString{"stack_duration"}) desc.stackPolicy = EffectStackPolicy::stack_duration;
      else if (spStr == SmallString{"independent"}) desc.stackPolicy = EffectStackPolicy::independent;
      else desc.stackPolicy = EffectStackPolicy::replace;

      return desc;
    }
  };

  // ---- Effect instance (runtime state) ----

  /// Runtime state for an active effect on an entity.
  struct EffectInstance {
    EffectInstanceId instanceId{};
    EffectDescriptorId descriptorId{};
    GameplayEntityId targetId{};   ///< Entity this effect is applied to
    GameplayEntityId sourceId{};   ///< Entity that applied this effect

    double durationRemaining{0.0};
    double periodTimer{0.0};       ///< Time until next periodic tick
    u32 stackCount{1};
    bool active{true};

    /// Number of periodic ticks that have occurred.
    u32 tickCount{0};
  };

  // ---- Effect events ----

  namespace effect_events {
    constexpr GameplayEventTypeId applied{200};
    constexpr GameplayEventTypeId removed{201};
    constexpr GameplayEventTypeId expired{202};
    constexpr GameplayEventTypeId periodic_tick{203};
    constexpr GameplayEventTypeId stacked{204};
    constexpr GameplayEventTypeId immune{205};
    constexpr GameplayEventTypeId cancelled{206};
  }  // namespace effect_events

  // ---- Stat block ----

  /// Holds base stat values and active modifiers, computes final values.
  ///
  /// StatBlock is a per-entity container for stats. It stores base values
  /// and collects modifiers from active effects. The final value for a stat
  /// is computed by applying all modifiers in order.
  class StatBlock {
  public:
    StatBlock() = default;

    /// Set the base value for a stat.
    void set_base(const SmallString &stat, double value) {
      _baseValues[std::string(stat.asChars())] = value;
    }

    /// Get the base value for a stat.
    double base(const SmallString &stat) const noexcept {
      auto it = _baseValues.find(std::string(stat.asChars()));
      return it != _baseValues.end() ? it->second : 0.0;
    }

    /// Add a modifier from an effect.
    void add_modifier(const StatModifier &mod) {
      _modifiers.push_back(mod);
    }

    /// Remove all modifiers from a given effect instance.
    size_t remove_modifiers_from(EffectInstanceId effectId) {
      size_t removed = 0;
      auto it = _modifiers.begin();
      while (it != _modifiers.end()) {
        if (it->sourceEffect == effectId) {
          it = _modifiers.erase(it);
          ++removed;
        } else {
          ++it;
        }
      }
      return removed;
    }

    /// Compute the final value for a stat after all modifiers.
    ///
    /// Evaluation order:
    ///   1. Start with base value
    ///   2. Apply base_override (last one wins)
    ///   3. Apply all additive modifiers
    ///   4. Apply all multiplicative modifiers
    ///   5. Apply final_override (last one wins)
    double compute(const SmallString &stat) const noexcept {
      double result = base(stat);
      auto key = std::string(stat.asChars());

      // Sort modifiers by priority (stable relative order)
      std::vector<const StatModifier *> relevant{};
      for (const auto &mod : _modifiers) {
        if (std::string(mod.statName.asChars()) == key) {
          relevant.push_back(&mod);
        }
      }
      std::sort(relevant.begin(), relevant.end(),
                [](const StatModifier *a, const StatModifier *b) {
                  return a->priority < b->priority;
                });

      // Phase 1: base_override
      for (const auto *mod : relevant) {
        if (mod->operation == StatModOp::base_override) {
          result = mod->value; // last one wins
        }
      }

      // Phase 2: additive
      for (const auto *mod : relevant) {
        if (mod->operation == StatModOp::additive) {
          result += mod->value;
        }
      }

      // Phase 3: multiplicative
      for (const auto *mod : relevant) {
        if (mod->operation == StatModOp::multiplicative) {
          result *= mod->value;
        }
      }

      // Phase 4: final_override
      for (const auto *mod : relevant) {
        if (mod->operation == StatModOp::final_override) {
          result = mod->value; // last one wins
        }
      }

      return result;
    }

    /// Get total modifier count.
    size_t modifier_count() const noexcept { return _modifiers.size(); }

    /// Get all stat names that have base values set.
    std::vector<SmallString> stat_names() const {
      std::vector<SmallString> names{};
      names.reserve(_baseValues.size());
      for (const auto &entry : _baseValues) {
        names.push_back(SmallString{entry.first.c_str()});
      }
      return names;
    }

    /// Clear all base values and modifiers.
    void clear() {
      _baseValues.clear();
      _modifiers.clear();
    }

  private:
    std::unordered_map<std::string, double> _baseValues{};
    std::vector<StatModifier> _modifiers{};
  };

  // ---- Callbacks ----

  /// Called when a periodic effect ticks. Receives the effect instance, descriptor,
  /// and the current stack count.
  using EffectPeriodicTickFn = std::function<void(const EffectInstance &instance,
                                                   const EffectDescriptor &descriptor)>;

  /// Called when an instant effect is applied.
  using EffectInstantApplyFn = std::function<void(const EffectInstance &instance,
                                                   const EffectDescriptor &descriptor)>;

  // ---- Effect system ----

  /// Manages effect descriptors, per-entity active effects, and stat modifiers.
  ///
  /// The EffectSystem:
  ///   1. Stores effect descriptors (templates)
  ///   2. Manages active effect instances per entity
  ///   3. Applies/removes stat modifiers via per-entity StatBlocks
  ///   4. Drives periodic ticks and duration expiry via tick()
  ///   5. Handles stacking, immunity, and cancellation
  class EffectSystem {
  public:
    EffectSystem() = default;

    // ---- Descriptor management ----

    bool register_descriptor(const EffectDescriptor &desc) {
      if (!desc.id.valid()) return false;
      auto [it, inserted] = _descriptors.emplace(desc.id.value, desc);
      return inserted;
    }

    const EffectDescriptor *find_descriptor(EffectDescriptorId id) const noexcept {
      auto it = _descriptors.find(id.value);
      return it != _descriptors.end() ? &it->second : nullptr;
    }

    bool remove_descriptor(EffectDescriptorId id) {
      return _descriptors.erase(id.value) > 0;
    }

    size_t descriptor_count() const noexcept { return _descriptors.size(); }

    // ---- Stat block management ----

    /// Get or create a stat block for an entity.
    StatBlock &stat_block(GameplayEntityId entityId) {
      return _statBlocks[entityId.value];
    }

    /// Get stat block (const). Returns nullptr if entity has no stat block.
    const StatBlock *find_stat_block(GameplayEntityId entityId) const noexcept {
      auto it = _statBlocks.find(entityId.value);
      return it != _statBlocks.end() ? &it->second : nullptr;
    }

    // ---- Effect application ----

    /// Apply an effect to a target entity. Returns the instance ID, or
    /// an invalid ID if the application was rejected (immunity, etc.).
    EffectInstanceId apply_effect(EffectDescriptorId descriptorId,
                                  GameplayEntityId targetId,
                                  GameplayEntityId sourceId = {},
                                  GameplayEventDispatcher *dispatcher = nullptr) {
      auto *desc = find_descriptor(descriptorId);
      if (!desc) return EffectInstanceId{0};

      // Check immunity: if the target has any tag listed in immunityTags
      auto &targetEffects = _entityEffects[targetId.value];
      if (check_immunity(targetId, *desc)) {
        if (dispatcher) {
          dispatcher->emit(effect_events::immune, SmallString{"effect.immune"},
                           sourceId, targetId, 0.0);
        }
        return EffectInstanceId{0};
      }

      // Handle cancellation: remove effects on target whose tags match
      // the new effect's cancellationTags
      if (!desc->cancellationTags.empty()) {
        cancel_matching_effects(targetId, desc->cancellationTags, dispatcher);
      }

      // Handle stacking for non-instant effects
      if (desc->durationType != EffectDurationType::instant) {
        auto *existing = find_existing_effect(targetId, descriptorId);
        if (existing) {
          auto stackResult = handle_stacking(*existing, *desc, dispatcher);
          // If handle_stacking returns a valid ID, stacking was handled
          // (replace, stack_count, stack_duration). If invalid, it means
          // independent policy: fall through to create a new instance.
          if (stackResult.valid()) return stackResult;
        }
      }

      // Create new instance
      EffectInstance inst{};
      inst.instanceId = EffectInstanceId{++_nextInstanceId};
      inst.descriptorId = descriptorId;
      inst.targetId = targetId;
      inst.sourceId = sourceId;
      inst.stackCount = 1;

      if (desc->durationType == EffectDurationType::instant) {
        // Instant: apply immediately, no ongoing state
        inst.active = false;
        if (_instantApplyFn) _instantApplyFn(inst, *desc);
        if (dispatcher) {
          dispatcher->emit(effect_events::applied, SmallString{"effect.applied"},
                           sourceId, targetId, desc->magnitude);
        }
        return inst.instanceId;
      }

      // Duration/periodic/infinite: set timers
      inst.durationRemaining = desc->duration;
      inst.periodTimer = desc->period;
      inst.active = true;

      // Apply stat modifiers
      apply_modifiers(targetId, inst.instanceId, *desc);

      // Grant tags
      // (Tags are tracked by checking active effects, not stored separately)

      targetEffects.push_back(inst);

      if (dispatcher) {
        dispatcher->emit(effect_events::applied, SmallString{"effect.applied"},
                         sourceId, targetId, desc->magnitude);
      }

      return inst.instanceId;
    }

    /// Remove an effect instance from an entity. Returns true if found.
    bool remove_effect(GameplayEntityId entityId, EffectInstanceId instanceId,
                       GameplayEventDispatcher *dispatcher = nullptr) {
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return false;

      auto &effects = it->second;
      for (size_t i = 0; i < effects.size(); ++i) {
        if (effects[i].instanceId == instanceId) {
          auto &inst = effects[i];
          // Remove stat modifiers
          auto sbIt = _statBlocks.find(entityId.value);
          if (sbIt != _statBlocks.end()) {
            sbIt->second.remove_modifiers_from(instanceId);
          }

          if (dispatcher) {
            dispatcher->emit(effect_events::removed, SmallString{"effect.removed"},
                             inst.sourceId, entityId, 0.0);
          }

          effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(i));
          if (effects.empty()) _entityEffects.erase(it);
          return true;
        }
      }
      return false;
    }

    /// Remove all effects from an entity.
    size_t remove_all_effects(GameplayEntityId entityId,
                               GameplayEventDispatcher *dispatcher = nullptr) {
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return 0;

      size_t count = it->second.size();

      // Remove modifiers
      auto sbIt = _statBlocks.find(entityId.value);
      if (sbIt != _statBlocks.end()) {
        for (const auto &inst : it->second) {
          sbIt->second.remove_modifiers_from(inst.instanceId);
        }
      }

      if (dispatcher) {
        for (const auto &inst : it->second) {
          dispatcher->emit(effect_events::removed, SmallString{"effect.removed"},
                           inst.sourceId, entityId, 0.0);
        }
      }

      _entityEffects.erase(it);
      return count;
    }

    // ---- Queries ----

    /// Find an effect instance on an entity.
    const EffectInstance *find_effect(GameplayEntityId entityId,
                                      EffectInstanceId instanceId) const noexcept {
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return nullptr;
      for (const auto &inst : it->second) {
        if (inst.instanceId == instanceId) return &inst;
      }
      return nullptr;
    }

    /// Get all active effects on an entity.
    std::vector<const EffectInstance *> entity_effects(
        GameplayEntityId entityId) const {
      std::vector<const EffectInstance *> result{};
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return result;
      result.reserve(it->second.size());
      for (const auto &inst : it->second) {
        result.push_back(&inst);
      }
      return result;
    }

    /// Count active effects on an entity.
    size_t effect_count(GameplayEntityId entityId) const noexcept {
      auto it = _entityEffects.find(entityId.value);
      return it != _entityEffects.end() ? it->second.size() : 0;
    }

    /// Check if an entity has any active effect with the given descriptor.
    bool has_effect(GameplayEntityId entityId,
                    EffectDescriptorId descriptorId) const noexcept {
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return false;
      for (const auto &inst : it->second) {
        if (inst.descriptorId == descriptorId) return true;
      }
      return false;
    }

    /// Check if an entity has a granted tag from any active effect.
    bool has_granted_tag(GameplayEntityId entityId,
                         const GameplayTag &tag) const noexcept {
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return false;
      for (const auto &inst : it->second) {
        if (!inst.active) continue;
        auto *desc = find_descriptor(inst.descriptorId);
        if (desc && desc->grantedTags.has(tag)) return true;
      }
      return false;
    }

    /// Total active effect instances across all entities.
    size_t total_effect_count() const noexcept {
      size_t total = 0;
      for (const auto &entry : _entityEffects) total += entry.second.size();
      return total;
    }

    // ---- Tick ----

    /// Advance all active effects by deltaTime seconds.
    void tick(double deltaTime, GameplayEventDispatcher *dispatcher = nullptr) {
      // Collect expired effects to remove after iteration
      std::vector<std::pair<GameplayEntityId, EffectInstanceId>> toRemove{};

      for (auto &entityEntry : _entityEffects) {
        GameplayEntityId entityId{entityEntry.first};
        for (auto &inst : entityEntry.second) {
          if (!inst.active) continue;

          auto *desc = find_descriptor(inst.descriptorId);
          if (!desc) continue;

          // Periodic tick
          if (desc->durationType == EffectDurationType::periodic && desc->period > 0.0) {
            inst.periodTimer -= deltaTime;
            while (inst.periodTimer <= 0.0) {
              inst.periodTimer += desc->period;
              ++inst.tickCount;
              if (_periodicTickFn) _periodicTickFn(inst, *desc);
              if (dispatcher) {
                dispatcher->emit(effect_events::periodic_tick,
                                 SmallString{"effect.tick"},
                                 inst.sourceId, entityId,
                                 desc->magnitude * static_cast<double>(inst.stackCount));
              }
            }
          }

          // Duration countdown (not for infinite effects)
          if (desc->durationType == EffectDurationType::duration
              || desc->durationType == EffectDurationType::periodic) {
            inst.durationRemaining -= deltaTime;
            if (inst.durationRemaining <= 0.0) {
              inst.active = false;
              toRemove.push_back({entityId, inst.instanceId});
              if (dispatcher) {
                dispatcher->emit(effect_events::expired, SmallString{"effect.expired"},
                                 inst.sourceId, entityId, 0.0);
              }
            }
          }
          // Infinite effects never expire on their own
        }
      }

      // Remove expired effects
      for (const auto &entry : toRemove) {
        remove_effect(entry.first, entry.second, nullptr);
      }
    }

    // ---- Callbacks ----

    void set_periodic_tick_callback(EffectPeriodicTickFn fn) {
      _periodicTickFn = static_cast<EffectPeriodicTickFn &&>(fn);
    }

    void set_instant_apply_callback(EffectInstantApplyFn fn) {
      _instantApplyFn = static_cast<EffectInstantApplyFn &&>(fn);
    }

    // ---- Clear ----

    void clear() {
      _descriptors.clear();
      _entityEffects.clear();
      _statBlocks.clear();
      _nextInstanceId = 0;
    }

  private:
    /// Check if the target is immune to the effect.
    bool check_immunity(GameplayEntityId targetId,
                        const EffectDescriptor &desc) const noexcept {
      if (desc.immunityTags.empty()) return false;

      // Check if any active effect on the target grants an immunity tag
      auto it = _entityEffects.find(targetId.value);
      if (it == _entityEffects.end()) return false;

      for (const auto &inst : it->second) {
        if (!inst.active) continue;
        auto *activeDesc = find_descriptor(inst.descriptorId);
        if (!activeDesc) continue;
        // Check if the active effect's granted tags contain any of the
        // new effect's immunity tags
        for (const auto &tag : desc.immunityTags.tags()) {
          if (activeDesc->grantedTags.has(tag)) return true;
        }
      }
      return false;
    }

    /// Find an existing active effect with the same descriptor on an entity.
    EffectInstance *find_existing_effect(GameplayEntityId entityId,
                                         EffectDescriptorId descriptorId) noexcept {
      auto it = _entityEffects.find(entityId.value);
      if (it == _entityEffects.end()) return nullptr;
      for (auto &inst : it->second) {
        if (inst.descriptorId == descriptorId && inst.active) return &inst;
      }
      return nullptr;
    }

    /// Handle stacking when an effect already exists on the target.
    EffectInstanceId handle_stacking(EffectInstance &existing,
                                      const EffectDescriptor &desc,
                                      GameplayEventDispatcher *dispatcher) {
      switch (desc.stackPolicy) {
        case EffectStackPolicy::replace:
          existing.durationRemaining = desc.duration;
          existing.periodTimer = desc.period;
          existing.tickCount = 0;
          if (dispatcher) {
            dispatcher->emit(effect_events::applied, SmallString{"effect.refreshed"},
                             existing.sourceId, existing.targetId, desc.magnitude);
          }
          return existing.instanceId;

        case EffectStackPolicy::stack_count:
          if (existing.stackCount < desc.maxStacks) {
            ++existing.stackCount;
            // Refresh duration
            existing.durationRemaining = desc.duration;
            if (dispatcher) {
              dispatcher->emit(effect_events::stacked, SmallString{"effect.stacked"},
                               existing.sourceId, existing.targetId,
                               static_cast<double>(existing.stackCount));
            }
          }
          return existing.instanceId;

        case EffectStackPolicy::stack_duration:
          existing.durationRemaining += desc.duration;
          if (dispatcher) {
            dispatcher->emit(effect_events::stacked, SmallString{"effect.extended"},
                             existing.sourceId, existing.targetId,
                             existing.durationRemaining);
          }
          return existing.instanceId;

        case EffectStackPolicy::independent:
          // Fall through to normal application (return invalid to signal
          // that we should create a new instance)
          return EffectInstanceId{0};

        default:
          return existing.instanceId;
      }
    }

    /// Cancel effects whose tags match the cancellation tag set.
    void cancel_matching_effects(GameplayEntityId targetId,
                                  const GameplayTagContainer &cancelTags,
                                  GameplayEventDispatcher *dispatcher) {
      auto it = _entityEffects.find(targetId.value);
      if (it == _entityEffects.end()) return;

      std::vector<EffectInstanceId> toCancel{};
      for (const auto &inst : it->second) {
        if (!inst.active) continue;
        auto *instDesc = find_descriptor(inst.descriptorId);
        if (!instDesc) continue;
        // If the existing effect has any tag that is a child of a cancellation tag
        for (const auto &cancelTag : cancelTags.tags()) {
          if (instDesc->tags.has(cancelTag) || instDesc->tags.has_any_child_of(cancelTag)) {
            toCancel.push_back(inst.instanceId);
            break;
          }
        }
      }

      for (auto eid : toCancel) {
        if (dispatcher) {
          dispatcher->emit(effect_events::cancelled, SmallString{"effect.cancelled"},
                           {}, targetId, 0.0);
        }
        remove_effect(targetId, eid, nullptr);
      }
    }

    /// Apply stat modifiers from an effect descriptor to an entity's stat block.
    void apply_modifiers(GameplayEntityId entityId, EffectInstanceId instanceId,
                          const EffectDescriptor &desc) {
      if (desc.modifiers.empty()) return;
      auto &sb = _statBlocks[entityId.value];
      for (const auto &mod : desc.modifiers) {
        StatModifier applied = mod;
        applied.sourceEffect = instanceId;
        sb.add_modifier(applied);
      }
    }

    std::unordered_map<u64, EffectDescriptor> _descriptors{};
    std::unordered_map<u64, std::vector<EffectInstance>> _entityEffects{};
    std::unordered_map<u64, StatBlock> _statBlocks{};
    u64 _nextInstanceId{0};

    EffectPeriodicTickFn _periodicTickFn{};
    EffectInstantApplyFn _instantApplyFn{};
  };

}  // namespace zs
