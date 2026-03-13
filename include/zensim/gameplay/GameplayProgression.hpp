#pragma once

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayEffect.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayMechanicsSchema.hpp"

namespace zs {

  // =====================================================================
  //  Progression Identification
  // =====================================================================

  /// Strongly-typed progression profile ID (one per entity).
  struct ProgressionProfileId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const ProgressionProfileId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const ProgressionProfileId &o) const noexcept {
      return value != o.value;
    }
  };

  /// Strongly-typed skill node ID for skill trees.
  struct SkillNodeId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const SkillNodeId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const SkillNodeId &o) const noexcept {
      return value != o.value;
    }
  };

  /// Strongly-typed skill tree ID.
  struct SkillTreeId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const SkillTreeId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const SkillTreeId &o) const noexcept {
      return value != o.value;
    }
  };

  // =====================================================================
  //  Level Threshold Table
  // =====================================================================

  /// A data-driven table mapping levels to cumulative XP thresholds.
  /// Level 1 requires 0 XP. Level N requires thresholds[N-2] cumulative XP.
  /// Example: thresholds = {100, 300, 600} means:
  ///   Level 1: 0 XP, Level 2: 100 XP, Level 3: 300 XP, Level 4: 600 XP
  struct LevelThresholdTable {
    std::vector<u64> thresholds;  ///< Cumulative XP for levels 2, 3, 4, ...
    u32 maxLevel{1};              ///< Maximum attainable level

    /// Create a table with explicit thresholds.
    static LevelThresholdTable from_thresholds(std::vector<u64> thresh) {
      LevelThresholdTable t;
      t.thresholds = std::move(thresh);
      t.maxLevel = static_cast<u32>(t.thresholds.size()) + 1;
      return t;
    }

    /// Create a simple linear table: level N requires baseXp * (N-1) cumulative.
    static LevelThresholdTable linear(u64 baseXpPerLevel, u32 levels) {
      LevelThresholdTable t;
      t.maxLevel = levels;
      t.thresholds.reserve(levels > 0 ? levels - 1 : 0);
      for (u32 i = 1; i < levels; ++i) {
        t.thresholds.push_back(baseXpPerLevel * static_cast<u64>(i));
      }
      return t;
    }

    /// Create a quadratic table: level N requires baseXp * (N-1)^2 cumulative.
    static LevelThresholdTable quadratic(u64 baseXp, u32 levels) {
      LevelThresholdTable t;
      t.maxLevel = levels;
      t.thresholds.reserve(levels > 0 ? levels - 1 : 0);
      for (u32 i = 1; i < levels; ++i) {
        t.thresholds.push_back(baseXp * static_cast<u64>(i) * static_cast<u64>(i));
      }
      return t;
    }

    /// Compute the level for a given cumulative XP amount.
    u32 level_for_xp(u64 xp) const noexcept {
      // Binary search: find how many thresholds the XP exceeds
      auto it = std::upper_bound(thresholds.begin(), thresholds.end(), xp);
      // If xp >= thresholds[k], then level is at least k+2
      // upper_bound returns first element > xp, so the count of elements <= xp is
      // the distance from begin to the returned iterator
      // But we want elements <= xp: use upper_bound which gives first > xp
      // Distance = number of thresholds that are <= xp
      u32 level = static_cast<u32>(
        std::distance(thresholds.begin(),
                      std::upper_bound(thresholds.begin(), thresholds.end(), xp - 1)));
      // Wait - we need elements where threshold <= xp
      // upper_bound(xp) returns first element > xp
      // So elements from [begin, upper_bound(xp)) are all <= xp
      level = static_cast<u32>(std::distance(thresholds.begin(), it));
      // Level is 1 + number of thresholds passed
      u32 result = level + 1;
      return result < maxLevel ? result : maxLevel;
    }

    /// Get the XP required for a specific level. Level 1 = 0.
    u64 xp_for_level(u32 level) const noexcept {
      if (level <= 1) return 0;
      u32 idx = level - 2;
      if (idx < static_cast<u32>(thresholds.size())) return thresholds[idx];
      // Beyond table: return last threshold or 0
      return thresholds.empty() ? 0 : thresholds.back();
    }

    /// Get XP needed from current XP to reach next level. 0 if at max.
    u64 xp_to_next_level(u64 currentXp) const noexcept {
      u32 currentLevel = level_for_xp(currentXp);
      if (currentLevel >= maxLevel) return 0;
      u64 nextThreshold = xp_for_level(currentLevel + 1);
      return currentXp >= nextThreshold ? 0 : nextThreshold - currentXp;
    }

    /// Get progress fraction [0, 1] within current level.
    double level_progress(u64 currentXp) const noexcept {
      u32 currentLevel = level_for_xp(currentXp);
      if (currentLevel >= maxLevel) return 1.0;
      u64 currentThreshold = xp_for_level(currentLevel);
      u64 nextThreshold = xp_for_level(currentLevel + 1);
      if (nextThreshold <= currentThreshold) return 1.0;
      return static_cast<double>(currentXp - currentThreshold)
             / static_cast<double>(nextThreshold - currentThreshold);
    }
  };

  // =====================================================================
  //  Stat Growth Curve
  // =====================================================================

  /// How a stat scales with level. Project layer provides the actual formulas;
  /// the framework provides the hook structure.
  enum class StatGrowthType : u8 {
    flat,          ///< stat += growthPerLevel * (level - 1)
    percentage,    ///< stat *= 1 + growthPerLevel * (level - 1)
    custom         ///< user-provided function
  };

  inline const char *stat_growth_type_name(StatGrowthType t) noexcept {
    switch (t) {
      case StatGrowthType::flat: return "flat";
      case StatGrowthType::percentage: return "percentage";
      case StatGrowthType::custom: return "custom";
      default: return "unknown";
    }
  }

  /// Describes how one stat grows with level.
  struct StatGrowthEntry {
    SmallString statName;
    StatGrowthType type{StatGrowthType::flat};
    double growthPerLevel{0.0};
    /// Custom growth function: (baseValue, level) -> modifier value.
    /// Only used when type == custom.
    std::function<double(double, u32)> customGrowth;
  };

  /// A collection of stat growth entries forming a "class" growth curve.
  struct StatGrowthTable {
    SmallString name;
    std::vector<StatGrowthEntry> entries;

    void add_flat(const SmallString &stat, double perLevel) {
      entries.push_back({stat, StatGrowthType::flat, perLevel, {}});
    }

    void add_percentage(const SmallString &stat, double perLevel) {
      entries.push_back({stat, StatGrowthType::percentage, perLevel, {}});
    }

    void add_custom(const SmallString &stat, std::function<double(double, u32)> fn) {
      entries.push_back({stat, StatGrowthType::custom, 0.0, std::move(fn)});
    }
  };

  // =====================================================================
  //  Skill Tree Node
  // =====================================================================

  /// Status of a skill node for a specific entity.
  enum class SkillNodeStatus : u8 {
    locked,      ///< Prerequisites not met
    available,   ///< Prerequisites met, can be unlocked
    unlocked     ///< Actively unlocked / purchased
  };

  inline const char *skill_node_status_name(SkillNodeStatus s) noexcept {
    switch (s) {
      case SkillNodeStatus::locked: return "locked";
      case SkillNodeStatus::available: return "available";
      case SkillNodeStatus::unlocked: return "unlocked";
      default: return "unknown";
    }
  }

  /// Describes one node in a skill tree. This is the template; each entity
  /// has its own unlock state tracked in ProgressionProfile.
  struct SkillNodeDescriptor {
    SkillNodeId id;
    SkillTreeId treeId;
    SmallString name;
    SmallString description;
    u32 requiredLevel{1};                       ///< Minimum level to unlock
    u32 pointCost{1};                           ///< Skill points required
    std::vector<SkillNodeId> prerequisites;     ///< Must be unlocked first
    /// Stat modifiers granted when unlocked (applied as additive by default).
    std::vector<std::pair<SmallString, double>> statBonuses;
    /// Tags granted when unlocked (e.g., "ability.fireball" to gate abilities).
    std::vector<SmallString> grantedTags;
    /// Optional effect descriptor to apply when unlocked.
    EffectDescriptorId grantedEffect;
    /// Maximum ranks for multi-rank nodes (1 = binary unlock).
    u32 maxRanks{1};
  };

  // =====================================================================
  //  Progression Profile (per-entity state)
  // =====================================================================

  /// Per-entity progression state.
  struct ProgressionProfile {
    GameplayEntityId entityId;
    u32 level{1};
    u64 totalXp{0};
    u32 skillPoints{0};        ///< Unspent skill points
    u32 totalSkillPoints{0};   ///< Total skill points ever earned

    /// Per-node unlock state: nodeId -> current rank (0 = not unlocked).
    std::unordered_map<u64, u32> skillRanks;

    /// Check if a node is unlocked (at least rank 1).
    bool is_skill_unlocked(SkillNodeId nodeId) const noexcept {
      auto it = skillRanks.find(nodeId.value);
      return it != skillRanks.end() && it->second > 0;
    }

    /// Get current rank of a skill node. 0 if not unlocked.
    u32 skill_rank(SkillNodeId nodeId) const noexcept {
      auto it = skillRanks.find(nodeId.value);
      return it != skillRanks.end() ? it->second : 0;
    }
  };

  // =====================================================================
  //  Progression Event Types
  // =====================================================================

  /// Well-known event type IDs for progression events.
  namespace progression_events {
    constexpr GameplayEventTypeId XP_GAINED{9001};
    constexpr GameplayEventTypeId LEVEL_UP{9002};
    constexpr GameplayEventTypeId SKILL_UNLOCKED{9003};
    constexpr GameplayEventTypeId SKILL_RANK_UP{9004};
    constexpr GameplayEventTypeId SKILL_POINTS_GAINED{9005};
  }  // namespace progression_events

  // =====================================================================
  //  Progression System
  // =====================================================================

  /// Central progression manager. Handles XP gain, level-up detection,
  /// stat growth application, and skill tree management.
  ///
  /// Design notes:
  /// - Stat growth modifiers use a dedicated source ID (EffectInstanceId with
  ///   a high-bit marker) so they can be cleanly removed and reapplied on level
  ///   change. They are applied as additive modifiers for flat growth and
  ///   multiplicative for percentage growth.
  /// - Skill tree nodes are registered globally; per-entity state is tracked
  ///   in ProgressionProfile.
  /// - Events are emitted through the dispatcher for XP gain, level-up, and
  ///   skill unlocks.
  class ProgressionSystem {
  public:
    // ---- Level threshold configuration ----

    /// Set the level threshold table used for all entities.
    void set_threshold_table(LevelThresholdTable table) {
      thresholdTable_ = std::move(table);
    }

    const LevelThresholdTable &threshold_table() const noexcept {
      return thresholdTable_;
    }

    // ---- Stat growth configuration ----

    /// Register a stat growth table (e.g., per character class).
    void register_growth_table(const SmallString &name, StatGrowthTable table) {
      table.name = name;
      growthTables_[std::string(name.asChars())] = std::move(table);
    }

    /// Find a registered growth table by name.
    const StatGrowthTable *find_growth_table(const SmallString &name) const noexcept {
      auto it = growthTables_.find(std::string(name.asChars()));
      return it != growthTables_.end() ? &it->second : nullptr;
    }

    /// Assign a growth table to an entity by name. Must be registered first.
    bool assign_growth_table(GameplayEntityId entityId, const SmallString &tableName) {
      if (growthTables_.find(std::string(tableName.asChars())) == growthTables_.end())
        return false;
      entityGrowthTable_[entityId.value] = std::string(tableName.asChars());
      return true;
    }

    // ---- Skill node registration ----

    /// Register a skill node descriptor. Returns false if ID already registered.
    bool register_skill_node(const SkillNodeDescriptor &desc) {
      if (!desc.id.valid()) return false;
      if (skillNodes_.find(desc.id.value) != skillNodes_.end()) return false;
      skillNodes_[desc.id.value] = desc;
      return true;
    }

    /// Find a registered skill node by ID.
    const SkillNodeDescriptor *find_skill_node(SkillNodeId id) const noexcept {
      auto it = skillNodes_.find(id.value);
      return it != skillNodes_.end() ? &it->second : nullptr;
    }

    /// Get all registered skill nodes for a given tree.
    std::vector<const SkillNodeDescriptor *> nodes_for_tree(SkillTreeId treeId) const {
      std::vector<const SkillNodeDescriptor *> result;
      for (auto &kv : skillNodes_) {
        if (kv.second.treeId == treeId) {
          result.push_back(&kv.second);
        }
      }
      return result;
    }

    // ---- Entity profile management ----

    /// Create or get the progression profile for an entity.
    ProgressionProfile &profile(GameplayEntityId entityId) {
      auto it = profiles_.find(entityId.value);
      if (it != profiles_.end()) return it->second;
      auto &p = profiles_[entityId.value];
      p.entityId = entityId;
      p.level = 1;
      p.totalXp = 0;
      p.skillPoints = 0;
      p.totalSkillPoints = 0;
      return p;
    }

    /// Find profile (const). Returns nullptr if not found.
    const ProgressionProfile *find_profile(GameplayEntityId entityId) const noexcept {
      auto it = profiles_.find(entityId.value);
      return it != profiles_.end() ? &it->second : nullptr;
    }

    /// Check if an entity has a progression profile.
    bool has_profile(GameplayEntityId entityId) const noexcept {
      return profiles_.find(entityId.value) != profiles_.end();
    }

    /// Remove an entity's progression profile.
    bool remove_profile(GameplayEntityId entityId) {
      return profiles_.erase(entityId.value) > 0;
    }

    /// Number of tracked entities.
    u32 profile_count() const noexcept {
      return static_cast<u32>(profiles_.size());
    }

    // ---- XP and Level Operations ----

    /// Award XP to an entity. Handles level-up detection and stat growth.
    /// Returns the number of levels gained (0 if none).
    /// Emits XP_GAINED and LEVEL_UP events through the dispatcher.
    u32 award_xp(GameplayEntityId entityId, u64 amount,
                 StatBlock *stats = nullptr,
                 GameplayEventDispatcher *dispatcher = nullptr) {
      auto &prof = profile(entityId);
      u32 oldLevel = prof.level;

      prof.totalXp += amount;
      u32 newLevel = thresholdTable_.level_for_xp(prof.totalXp);
      u32 levelsGained = 0;

      // Emit XP gained event
      if (dispatcher) {
        GameplayEvent evt;
        evt.typeId = progression_events::XP_GAINED;
        evt.typeName = SmallString{"xp_gained"};
        evt.source = entityId;
        evt.target = entityId;
        evt.numericValue = static_cast<double>(amount);
        evt.stringValue = SmallString{""};
        dispatcher->dispatch(evt);
      }

      // Process level-ups one at a time
      if (newLevel > oldLevel) {
        levelsGained = newLevel - oldLevel;

        // Award skill points for each level gained
        for (u32 l = oldLevel + 1; l <= newLevel; ++l) {
          u32 pointsForLevel = skill_points_per_level(l);
          prof.skillPoints += pointsForLevel;
          prof.totalSkillPoints += pointsForLevel;

          if (dispatcher && pointsForLevel > 0) {
            GameplayEvent spEvt;
            spEvt.typeId = progression_events::SKILL_POINTS_GAINED;
            spEvt.typeName = SmallString{"skill_points_gained"};
            spEvt.source = entityId;
            spEvt.target = entityId;
            spEvt.numericValue = static_cast<double>(pointsForLevel);
            spEvt.stringValue = SmallString{""};
            dispatcher->dispatch(spEvt);
          }
        }

        prof.level = newLevel;

        // Reapply stat growth modifiers
        if (stats) {
          apply_stat_growth(entityId, *stats);
        }

        // Emit level-up events (one per level gained)
        if (dispatcher) {
          for (u32 l = oldLevel + 1; l <= newLevel; ++l) {
            GameplayEvent evt;
            evt.typeId = progression_events::LEVEL_UP;
            evt.typeName = SmallString{"level_up"};
            evt.source = entityId;
            evt.target = entityId;
            evt.numericValue = static_cast<double>(l);
            evt.stringValue = SmallString{""};
            dispatcher->dispatch(evt);
          }
        }
      }

      return levelsGained;
    }

    /// Set an entity's level directly (e.g., for debug/testing).
    /// Updates XP to match the level threshold.
    void set_level(GameplayEntityId entityId, u32 level,
                   StatBlock *stats = nullptr) {
      auto &prof = profile(entityId);
      prof.level = level;
      prof.totalXp = thresholdTable_.xp_for_level(level);
      if (stats) {
        apply_stat_growth(entityId, *stats);
      }
    }

    /// Get the current level for an entity.
    u32 level(GameplayEntityId entityId) const noexcept {
      auto it = profiles_.find(entityId.value);
      return it != profiles_.end() ? it->second.level : 1;
    }

    /// Get total XP for an entity.
    u64 total_xp(GameplayEntityId entityId) const noexcept {
      auto it = profiles_.find(entityId.value);
      return it != profiles_.end() ? it->second.totalXp : 0;
    }

    // ---- Stat Growth Application ----

    /// Apply (or reapply) stat growth modifiers to a stat block based on
    /// the entity's current level and assigned growth table.
    void apply_stat_growth(GameplayEntityId entityId, StatBlock &stats) {
      // Remove old progression modifiers
      EffectInstanceId sourceId = progression_source_id(entityId);
      stats.remove_modifiers_from(sourceId);

      auto profIt = profiles_.find(entityId.value);
      if (profIt == profiles_.end()) return;
      u32 currentLevel = profIt->second.level;
      if (currentLevel <= 1) return;  // No growth at level 1

      // Find the growth table assigned to this entity
      auto tableIt = entityGrowthTable_.find(entityId.value);
      if (tableIt == entityGrowthTable_.end()) return;
      auto growthIt = growthTables_.find(tableIt->second);
      if (growthIt == growthTables_.end()) return;

      const StatGrowthTable &table = growthIt->second;

      for (auto &entry : table.entries) {
        double modValue = 0.0;
        StatModOp op = StatModOp::additive;

        switch (entry.type) {
          case StatGrowthType::flat:
            modValue = entry.growthPerLevel * static_cast<double>(currentLevel - 1);
            op = StatModOp::additive;
            break;
          case StatGrowthType::percentage:
            modValue = 1.0 + entry.growthPerLevel * static_cast<double>(currentLevel - 1);
            op = StatModOp::multiplicative;
            break;
          case StatGrowthType::custom:
            if (entry.customGrowth) {
              double base = stats.base(entry.statName);
              modValue = entry.customGrowth(base, currentLevel);
              op = StatModOp::additive;
            }
            break;
        }

        StatModifier mod;
        mod.statName = entry.statName;
        mod.operation = op;
        mod.value = modValue;
        mod.sourceEffect = sourceId;
        mod.priority = -1000;  // Low priority: growth applies early
        stats.add_modifier(mod);
      }
    }

    // ---- Skill Points Configuration ----

    /// Set skill points awarded per level. Default: 1 point per level.
    void set_skill_points_per_level(u32 points) {
      skillPointsPerLevel_ = points;
      customSkillPointsFn_ = {};
    }

    /// Set a custom function for skill points per level.
    /// Function receives the level number, returns points awarded.
    void set_skill_points_function(std::function<u32(u32)> fn) {
      customSkillPointsFn_ = std::move(fn);
    }

    /// Get skill points awarded at a specific level.
    u32 skill_points_per_level(u32 level) const {
      if (customSkillPointsFn_) return customSkillPointsFn_(level);
      return skillPointsPerLevel_;
    }

    // ---- Skill Tree Operations ----

    /// Result of attempting to unlock a skill node.
    enum class UnlockResult : u8 {
      success,
      already_max_rank,
      insufficient_level,
      insufficient_points,
      prerequisites_not_met,
      node_not_found,
      profile_not_found
    };

    static const char *unlock_result_name(UnlockResult r) noexcept {
      switch (r) {
        case UnlockResult::success: return "success";
        case UnlockResult::already_max_rank: return "already_max_rank";
        case UnlockResult::insufficient_level: return "insufficient_level";
        case UnlockResult::insufficient_points: return "insufficient_points";
        case UnlockResult::prerequisites_not_met: return "prerequisites_not_met";
        case UnlockResult::node_not_found: return "node_not_found";
        case UnlockResult::profile_not_found: return "profile_not_found";
        default: return "unknown";
      }
    }

    /// Check the status of a skill node for a given entity.
    SkillNodeStatus node_status(GameplayEntityId entityId, SkillNodeId nodeId) const {
      auto nodeIt = skillNodes_.find(nodeId.value);
      if (nodeIt == skillNodes_.end()) return SkillNodeStatus::locked;

      auto profIt = profiles_.find(entityId.value);
      if (profIt == profiles_.end()) return SkillNodeStatus::locked;

      const auto &node = nodeIt->second;
      const auto &prof = profIt->second;

      // Already unlocked?
      u32 rank = prof.skill_rank(nodeId);
      if (rank >= node.maxRanks) return SkillNodeStatus::unlocked;
      if (rank > 0) return SkillNodeStatus::unlocked;  // partially ranked = unlocked

      // Check prerequisites
      if (!check_prerequisites(prof, node)) return SkillNodeStatus::locked;

      // Check level requirement
      if (prof.level < node.requiredLevel) return SkillNodeStatus::locked;

      return SkillNodeStatus::available;
    }

    /// Attempt to unlock (or rank up) a skill node.
    UnlockResult unlock_skill(GameplayEntityId entityId, SkillNodeId nodeId,
                              StatBlock *stats = nullptr,
                              EffectSystem *effects = nullptr,
                              GameplayEventDispatcher *dispatcher = nullptr) {
      auto nodeIt = skillNodes_.find(nodeId.value);
      if (nodeIt == skillNodes_.end()) return UnlockResult::node_not_found;

      auto profIt = profiles_.find(entityId.value);
      if (profIt == profiles_.end()) return UnlockResult::profile_not_found;

      const auto &node = nodeIt->second;
      auto &prof = profIt->second;

      // Check max rank
      u32 currentRank = prof.skill_rank(nodeId);
      if (currentRank >= node.maxRanks) return UnlockResult::already_max_rank;

      // Check level
      if (prof.level < node.requiredLevel) return UnlockResult::insufficient_level;

      // Check prerequisites
      if (!check_prerequisites(prof, node)) return UnlockResult::prerequisites_not_met;

      // Check points
      if (prof.skillPoints < node.pointCost) return UnlockResult::insufficient_points;

      // Unlock!
      prof.skillPoints -= node.pointCost;
      prof.skillRanks[nodeId.value] = currentRank + 1;

      // Apply stat bonuses
      if (stats) {
        apply_skill_stat_bonuses(entityId, node, currentRank + 1, *stats);
      }

      // Apply granted effect
      if (effects && node.grantedEffect.valid()) {
        effects->apply_effect(node.grantedEffect, entityId, entityId);
      }

      // Emit event
      if (dispatcher) {
        GameplayEventTypeId evtType = (currentRank == 0)
          ? progression_events::SKILL_UNLOCKED
          : progression_events::SKILL_RANK_UP;

        GameplayEvent evt;
        evt.typeId = evtType;
        evt.typeName = (currentRank == 0)
          ? SmallString{"skill_unlocked"}
          : SmallString{"skill_rank_up"};
        evt.source = entityId;
        evt.target = entityId;
        evt.numericValue = static_cast<double>(currentRank + 1);
        evt.stringValue = node.name;
        dispatcher->dispatch(evt);
      }

      return UnlockResult::success;
    }

    /// Reset all skill points for an entity. Returns all spent points.
    /// Removes all skill-granted stat modifiers.
    u32 reset_skills(GameplayEntityId entityId, StatBlock *stats = nullptr) {
      auto profIt = profiles_.find(entityId.value);
      if (profIt == profiles_.end()) return 0;
      auto &prof = profIt->second;

      u32 refundedPoints = 0;
      for (auto &kv : prof.skillRanks) {
        auto nodeIt = skillNodes_.find(kv.first);
        if (nodeIt != skillNodes_.end()) {
          refundedPoints += nodeIt->second.pointCost * kv.second;
        }
      }

      prof.skillRanks.clear();
      prof.skillPoints += refundedPoints;

      // Remove skill stat modifiers
      if (stats) {
        EffectInstanceId sourceId = skill_source_id(entityId);
        stats->remove_modifiers_from(sourceId);
      }

      return refundedPoints;
    }

    /// Get all unlocked skill node IDs for an entity.
    std::vector<SkillNodeId> unlocked_skills(GameplayEntityId entityId) const {
      std::vector<SkillNodeId> result;
      auto profIt = profiles_.find(entityId.value);
      if (profIt == profiles_.end()) return result;
      for (auto &kv : profIt->second.skillRanks) {
        if (kv.second > 0) {
          result.push_back(SkillNodeId{kv.first});
        }
      }
      return result;
    }

    /// Get available (unlockable) skill nodes for an entity.
    std::vector<SkillNodeId> available_skills(GameplayEntityId entityId) const {
      std::vector<SkillNodeId> result;
      auto profIt = profiles_.find(entityId.value);
      if (profIt == profiles_.end()) return result;
      for (auto &kv : skillNodes_) {
        if (node_status(entityId, SkillNodeId{kv.first}) == SkillNodeStatus::available) {
          result.push_back(SkillNodeId{kv.first});
        }
      }
      return result;
    }

    // ---- Utility ----

    /// Clear all state.
    void clear() {
      profiles_.clear();
      growthTables_.clear();
      entityGrowthTable_.clear();
      skillNodes_.clear();
      thresholdTable_ = {};
      skillPointsPerLevel_ = 1;
      customSkillPointsFn_ = {};
    }

    /// Clear only entity state, keeping configuration (tables, nodes).
    void clear_profiles() {
      profiles_.clear();
      entityGrowthTable_.clear();
    }

    /// Total registered skill nodes.
    u32 skill_node_count() const noexcept {
      return static_cast<u32>(skillNodes_.size());
    }

    /// Total registered growth tables.
    u32 growth_table_count() const noexcept {
      return static_cast<u32>(growthTables_.size());
    }

  private:
    /// Generate a deterministic EffectInstanceId for progression stat growth.
    /// Uses a high-bit marker to avoid collision with real effect instances.
    static EffectInstanceId progression_source_id(GameplayEntityId entityId) noexcept {
      return EffectInstanceId{0x8000000000000000ULL | entityId.value};
    }

    /// Generate a deterministic EffectInstanceId for skill stat bonuses.
    static EffectInstanceId skill_source_id(GameplayEntityId entityId) noexcept {
      return EffectInstanceId{0x4000000000000000ULL | entityId.value};
    }

    /// Check if all prerequisites for a node are met by the profile.
    bool check_prerequisites(const ProgressionProfile &prof,
                             const SkillNodeDescriptor &node) const noexcept {
      for (auto &prereqId : node.prerequisites) {
        if (!prof.is_skill_unlocked(prereqId)) return false;
      }
      return true;
    }

    /// Apply stat bonuses from a skill node.
    void apply_skill_stat_bonuses(GameplayEntityId entityId,
                                  const SkillNodeDescriptor &node,
                                  u32 rank,
                                  StatBlock &stats) {
      EffectInstanceId sourceId = skill_source_id(entityId);
      // Remove old modifiers from this source and reapply for current rank
      // (For simplicity, we accumulate — multi-rank nodes get rank * bonus)
      for (auto &bonus : node.statBonuses) {
        StatModifier mod;
        mod.statName = bonus.first;
        mod.operation = StatModOp::additive;
        mod.value = bonus.second * static_cast<double>(rank);
        mod.sourceEffect = sourceId;
        mod.priority = -500;  // After growth, before equipment
        stats.add_modifier(mod);
      }
    }

    // ---- State ----
    LevelThresholdTable thresholdTable_;
    std::unordered_map<u64, ProgressionProfile> profiles_;
    std::unordered_map<std::string, StatGrowthTable> growthTables_;
    std::unordered_map<u64, std::string> entityGrowthTable_;  // entityId -> table name
    std::unordered_map<u64, SkillNodeDescriptor> skillNodes_;
    u32 skillPointsPerLevel_{1};
    std::function<u32(u32)> customSkillPointsFn_;
  };

}  // namespace zs
