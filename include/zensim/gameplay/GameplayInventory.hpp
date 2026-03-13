#pragma once

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayEffect.hpp"
#include "zensim/gameplay/GameplayEntity.hpp"
#include "zensim/gameplay/GameplayEvent.hpp"
#include "zensim/gameplay/GameplayMechanicsSchema.hpp"

namespace zs {

  // ---- Inventory / equipment event type IDs ----

  namespace inventory_events {
    constexpr GameplayEventTypeId item_added{400};
    constexpr GameplayEventTypeId item_removed{401};
    constexpr GameplayEventTypeId item_moved{402};
    constexpr GameplayEventTypeId item_stacked{403};
    constexpr GameplayEventTypeId item_split{404};
    constexpr GameplayEventTypeId item_equipped{405};
    constexpr GameplayEventTypeId item_unequipped{406};
    constexpr GameplayEventTypeId inventory_full{407};
  }  // namespace inventory_events

  // ---- Item identification ----

  /// Strongly-typed item descriptor ID (shared across all instances of the same
  /// item type).
  struct ItemDescriptorId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const ItemDescriptorId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const ItemDescriptorId &o) const noexcept {
      return value != o.value;
    }
  };

  /// Strongly-typed item instance ID (globally unique per instance).
  struct ItemInstanceId {
    u64 value{0};
    constexpr bool valid() const noexcept { return value != 0; }
    constexpr bool operator==(const ItemInstanceId &o) const noexcept {
      return value == o.value;
    }
    constexpr bool operator!=(const ItemInstanceId &o) const noexcept {
      return value != o.value;
    }
  };

  // ---- Item rarity ----

  enum class ItemRarity : u8 {
    common,
    uncommon,
    rare,
    epic,
    legendary
  };

  inline const char *item_rarity_name(ItemRarity r) noexcept {
    switch (r) {
      case ItemRarity::common: return "common";
      case ItemRarity::uncommon: return "uncommon";
      case ItemRarity::rare: return "rare";
      case ItemRarity::epic: return "epic";
      case ItemRarity::legendary: return "legendary";
      default: return "unknown";
    }
  }

  // ---- Equipment slot ----

  enum class EquipmentSlot : u8 {
    none,
    head,
    chest,
    legs,
    feet,
    hands,
    main_hand,
    off_hand,
    ring,
    amulet
  };

  inline const char *equipment_slot_name(EquipmentSlot s) noexcept {
    switch (s) {
      case EquipmentSlot::none: return "none";
      case EquipmentSlot::head: return "head";
      case EquipmentSlot::chest: return "chest";
      case EquipmentSlot::legs: return "legs";
      case EquipmentSlot::feet: return "feet";
      case EquipmentSlot::hands: return "hands";
      case EquipmentSlot::main_hand: return "main_hand";
      case EquipmentSlot::off_hand: return "off_hand";
      case EquipmentSlot::ring: return "ring";
      case EquipmentSlot::amulet: return "amulet";
      default: return "unknown";
    }
  }

  inline EquipmentSlot equipment_slot_from_string(const SmallString &s) noexcept {
    if (s == SmallString{"head"}) return EquipmentSlot::head;
    if (s == SmallString{"chest"}) return EquipmentSlot::chest;
    if (s == SmallString{"legs"}) return EquipmentSlot::legs;
    if (s == SmallString{"feet"}) return EquipmentSlot::feet;
    if (s == SmallString{"hands"}) return EquipmentSlot::hands;
    if (s == SmallString{"main_hand"}) return EquipmentSlot::main_hand;
    if (s == SmallString{"off_hand"}) return EquipmentSlot::off_hand;
    if (s == SmallString{"ring"}) return EquipmentSlot::ring;
    if (s == SmallString{"amulet"}) return EquipmentSlot::amulet;
    return EquipmentSlot::none;
  }

  // ---- Item descriptor (data-driven definition) ----

  /// Describes the static properties of an item type. Loaded from data.
  struct ItemDescriptor {
    ItemDescriptorId id{};
    SmallString name{};
    SmallString category{};               ///< E.g. "weapon", "armor", "consumable"
    GameplayTagContainer tags{};
    ItemRarity rarity{ItemRarity::common};
    EquipmentSlot slot{EquipmentSlot::none};

    u32 maxStackSize{1};                  ///< 1 = not stackable
    double weight{0.0};                   ///< Per-unit weight
    double value{0.0};                    ///< Per-unit monetary value

    /// Stat modifiers applied while this item is equipped.
    std::vector<StatModifier> equipModifiers{};

    /// Effects granted while this item is equipped (descriptor IDs).
    std::vector<EffectDescriptorId> equipEffects{};

    /// Effects applied when this item is consumed (descriptor IDs).
    std::vector<EffectDescriptorId> useEffects{};

    /// Whether this item can be equipped.
    bool is_equippable() const noexcept {
      return slot != EquipmentSlot::none;
    }

    /// Whether this item is stackable.
    bool is_stackable() const noexcept { return maxStackSize > 1; }

    /// Create an ItemDescriptor from a MechanicsDefinition.
    static ItemDescriptor from_definition(const MechanicsDefinition &def,
                                           ItemDescriptorId descriptorId) {
      ItemDescriptor desc{};
      desc.id = descriptorId;
      desc.name = def.string_field(SmallString{"name"}, def.id);
      desc.category = def.string_field(SmallString{"category"}, SmallString{});
      desc.tags = def.tags;

      desc.maxStackSize = static_cast<u32>(
          def.integer_field(SmallString{"max_stack"}, 1));
      if (desc.maxStackSize == 0) desc.maxStackSize = 1;

      desc.weight = def.number_field(SmallString{"weight"}, 0.0);
      desc.value = def.number_field(SmallString{"value"}, 0.0);

      // Rarity
      auto rarityStr = def.string_field(SmallString{"rarity"}, SmallString{"common"});
      if (rarityStr == SmallString{"uncommon"}) desc.rarity = ItemRarity::uncommon;
      else if (rarityStr == SmallString{"rare"}) desc.rarity = ItemRarity::rare;
      else if (rarityStr == SmallString{"epic"}) desc.rarity = ItemRarity::epic;
      else if (rarityStr == SmallString{"legendary"}) desc.rarity = ItemRarity::legendary;
      else desc.rarity = ItemRarity::common;

      // Equipment slot
      auto slotStr = def.string_field(SmallString{"slot"}, SmallString{"none"});
      desc.slot = equipment_slot_from_string(slotStr);

      return desc;
    }
  };

  // ---- Item instance (runtime state) ----

  /// A single item instance in an inventory. May represent a stack.
  struct ItemInstance {
    ItemInstanceId instanceId{};
    ItemDescriptorId descriptorId{};
    u32 stackCount{1};                    ///< Current stack size

    /// Per-instance data (e.g., durability, enchantments). Kept minimal
    /// for the framework layer; project code can extend via tag data.
    GameplayTagContainer instanceTags{};
  };

  // ---- Inventory container ----

  /// A fixed-capacity container of item instances with slot semantics.
  ///
  /// The Inventory manages a collection of item slots, each holding one
  /// ItemInstance (which may be a stack). Operations emit events and
  /// respect capacity and stacking rules.
  class Inventory {
  public:
    Inventory() = default;

    /// Construct with a maximum slot capacity.
    explicit Inventory(u32 capacity) : _capacity{capacity} {}

    // ---- Capacity ----

    u32 capacity() const noexcept { return _capacity; }
    void set_capacity(u32 cap) noexcept { _capacity = cap; }
    u32 used_slots() const noexcept { return static_cast<u32>(_items.size()); }
    u32 free_slots() const noexcept {
      return _capacity > used_slots() ? _capacity - used_slots() : 0;
    }
    bool is_full() const noexcept { return used_slots() >= _capacity; }
    bool is_empty() const noexcept { return _items.empty(); }

    // ---- Item registration (descriptors must be looked up externally) ----

    /// Add an item to the inventory. Handles stacking if the descriptor
    /// allows it. Returns the instance ID of the item (new or existing
    /// stack). Returns invalid ID if the inventory is full and cannot stack.
    ///
    /// The caller must provide the descriptor for stacking decisions.
    ItemInstanceId add_item(ItemDescriptorId descriptorId,
                            u32 count,
                            const ItemDescriptor *desc,
                            u64 &nextInstanceId,
                            GameplayEventDispatcher *dispatcher = nullptr) {
      if (count == 0) return ItemInstanceId{0};

      // Try to stack onto existing items first
      if (desc && desc->is_stackable()) {
        for (auto &item : _items) {
          if (item.descriptorId == descriptorId) {
            u32 space = desc->maxStackSize - item.stackCount;
            if (space > 0) {
              u32 toAdd = count < space ? count : space;
              item.stackCount += toAdd;
              count -= toAdd;
              if (dispatcher) {
                dispatcher->emit(inventory_events::item_stacked,
                                 SmallString{"inv.stacked"},
                                 {}, {}, static_cast<double>(item.stackCount));
              }
              if (count == 0) return item.instanceId;
            }
          }
        }
      }

      // Create new stacks for remaining count
      ItemInstanceId lastId{0};
      u32 stackSize = desc ? desc->maxStackSize : 1;
      if (stackSize == 0) stackSize = 1;

      while (count > 0) {
        if (is_full()) {
          if (dispatcher) {
            dispatcher->emit(inventory_events::inventory_full,
                             SmallString{"inv.full"},
                             {}, {}, static_cast<double>(count));
          }
          return lastId;  // Return last successfully created, or invalid
        }

        ItemInstance inst{};
        inst.instanceId = ItemInstanceId{++nextInstanceId};
        inst.descriptorId = descriptorId;
        inst.stackCount = count < stackSize ? count : stackSize;
        count -= inst.stackCount;

        lastId = inst.instanceId;
        _items.push_back(static_cast<ItemInstance &&>(inst));

        if (dispatcher) {
          dispatcher->emit(inventory_events::item_added,
                           SmallString{"inv.added"},
                           {}, {}, static_cast<double>(lastId.value));
        }
      }

      return lastId;
    }

    /// Remove a specific quantity from an item instance. If the entire
    /// stack is consumed, the slot is freed. Returns the actual amount
    /// removed.
    u32 remove_item(ItemInstanceId instanceId, u32 count,
                    GameplayEventDispatcher *dispatcher = nullptr) {
      for (size_t i = 0; i < _items.size(); ++i) {
        if (_items[i].instanceId == instanceId) {
          u32 actual = count < _items[i].stackCount ? count : _items[i].stackCount;
          _items[i].stackCount -= actual;

          if (_items[i].stackCount == 0) {
            _items.erase(_items.begin() + static_cast<std::ptrdiff_t>(i));
          }

          if (dispatcher) {
            dispatcher->emit(inventory_events::item_removed,
                             SmallString{"inv.removed"},
                             {}, {}, static_cast<double>(actual));
          }
          return actual;
        }
      }
      return 0;
    }

    /// Remove items by descriptor ID. Removes from stacks in order.
    /// Returns total amount removed.
    u32 remove_item_by_descriptor(ItemDescriptorId descriptorId, u32 count,
                                   GameplayEventDispatcher *dispatcher = nullptr) {
      u32 totalRemoved = 0;

      auto it = _items.begin();
      while (it != _items.end() && count > 0) {
        if (it->descriptorId == descriptorId) {
          u32 actual = count < it->stackCount ? count : it->stackCount;
          it->stackCount -= actual;
          count -= actual;
          totalRemoved += actual;

          if (it->stackCount == 0) {
            it = _items.erase(it);
          } else {
            ++it;
          }
        } else {
          ++it;
        }
      }

      if (totalRemoved > 0 && dispatcher) {
        dispatcher->emit(inventory_events::item_removed,
                         SmallString{"inv.removed"},
                         {}, {}, static_cast<double>(totalRemoved));
      }

      return totalRemoved;
    }

    // ---- Queries ----

    /// Find an item instance by ID.
    const ItemInstance *find_item(ItemInstanceId instanceId) const noexcept {
      for (const auto &item : _items) {
        if (item.instanceId == instanceId) return &item;
      }
      return nullptr;
    }

    /// Count total quantity of a specific item descriptor in the inventory.
    u32 count_item(ItemDescriptorId descriptorId) const noexcept {
      u32 total = 0;
      for (const auto &item : _items) {
        if (item.descriptorId == descriptorId) {
          total += item.stackCount;
        }
      }
      return total;
    }

    /// Check if the inventory contains at least `count` of a descriptor.
    bool has_item(ItemDescriptorId descriptorId, u32 count = 1) const noexcept {
      return count_item(descriptorId) >= count;
    }

    /// Get all item instances.
    const std::vector<ItemInstance> &items() const noexcept { return _items; }

    /// Get all instances of a specific descriptor.
    std::vector<const ItemInstance *> items_of(ItemDescriptorId descriptorId) const {
      std::vector<const ItemInstance *> result{};
      for (const auto &item : _items) {
        if (item.descriptorId == descriptorId) result.push_back(&item);
      }
      return result;
    }

    /// Total number of individual items (sum of all stack counts).
    u32 total_item_count() const noexcept {
      u32 total = 0;
      for (const auto &item : _items) total += item.stackCount;
      return total;
    }

    /// Split a stack into two. Returns the ID of the new stack,
    /// or invalid if the split is not possible.
    ItemInstanceId split_stack(ItemInstanceId instanceId, u32 splitCount,
                               u64 &nextInstanceId,
                               GameplayEventDispatcher *dispatcher = nullptr) {
      if (is_full()) return ItemInstanceId{0};  // No room for new stack

      for (auto &item : _items) {
        if (item.instanceId == instanceId) {
          if (splitCount == 0 || splitCount >= item.stackCount) {
            return ItemInstanceId{0};  // Invalid split
          }

          item.stackCount -= splitCount;

          ItemInstance newStack{};
          newStack.instanceId = ItemInstanceId{++nextInstanceId};
          newStack.descriptorId = item.descriptorId;
          newStack.stackCount = splitCount;
          newStack.instanceTags = item.instanceTags;

          auto newId = newStack.instanceId;
          _items.push_back(static_cast<ItemInstance &&>(newStack));

          if (dispatcher) {
            dispatcher->emit(inventory_events::item_split,
                             SmallString{"inv.split"},
                             {}, {}, static_cast<double>(splitCount));
          }

          return newId;
        }
      }
      return ItemInstanceId{0};
    }

    /// Clear all items.
    void clear() { _items.clear(); }

  private:
    std::vector<ItemInstance> _items{};
    u32 _capacity{20};  ///< Default 20 slots
  };

  // ---- Equipment system ----

  /// Manages equipped items for entities, integrating with StatBlock and
  /// EffectSystem for stat modifications and equipment-granted effects.
  ///
  /// The EquipmentSystem:
  ///   1. Tracks which item is in each equipment slot per entity
  ///   2. Applies stat modifiers from equipped items to the entity's StatBlock
  ///   3. Applies/removes equipment-granted effects via the EffectSystem
  ///   4. Emits events on equip/unequip
  class EquipmentSystem {
  public:
    EquipmentSystem() = default;

    // ---- System references ----

    void set_effect_system(EffectSystem *sys) noexcept { _effectSystem = sys; }

    // ---- Descriptor management ----

    bool register_descriptor(const ItemDescriptor &desc) {
      if (!desc.id.valid()) return false;
      auto [it, inserted] = _descriptors.emplace(desc.id.value, desc);
      return inserted;
    }

    const ItemDescriptor *find_descriptor(ItemDescriptorId id) const noexcept {
      auto it = _descriptors.find(id.value);
      return it != _descriptors.end() ? &it->second : nullptr;
    }

    bool remove_descriptor(ItemDescriptorId id) {
      return _descriptors.erase(id.value) > 0;
    }

    size_t descriptor_count() const noexcept { return _descriptors.size(); }

    // ---- Equip / unequip ----

    /// Equip an item to an entity's slot. The item must be equippable
    /// (slot != none) and the slot must be empty. Returns true on success.
    ///
    /// On success:
    ///   - Stat modifiers from the item are applied to the entity's StatBlock
    ///   - Equipment-granted effects are applied via EffectSystem
    ///   - An equip event is emitted
    bool equip(GameplayEntityId entityId, ItemInstanceId itemInstanceId,
               const ItemDescriptor &desc,
               GameplayEventDispatcher *dispatcher = nullptr) {
      if (!desc.is_equippable()) return false;

      auto &slots = _entityEquipment[entityId.value];

      // Check if slot is already occupied
      auto slotKey = static_cast<u8>(desc.slot);
      if (slots.find(slotKey) != slots.end()) return false;

      EquippedItem equipped{};
      equipped.itemInstanceId = itemInstanceId;
      equipped.descriptorId = desc.id;
      equipped.slot = desc.slot;

      // Apply stat modifiers
      if (_effectSystem && !desc.equipModifiers.empty()) {
        auto &statBlock = _effectSystem->stat_block(entityId);
        for (const auto &mod : desc.equipModifiers) {
          StatModifier applied = mod;
          // Use a synthetic effect instance ID derived from the item instance
          // to allow removal later. We use a fixed offset to avoid collision.
          applied.sourceEffect = EffectInstanceId{itemInstanceId.value + 0x10000000ULL};
          statBlock.add_modifier(applied);
        }
        equipped.syntheticEffectId = EffectInstanceId{
            itemInstanceId.value + 0x10000000ULL};
      }

      // Apply equipment-granted effects
      if (_effectSystem) {
        for (const auto &effectId : desc.equipEffects) {
          auto instId = _effectSystem->apply_effect(effectId, entityId, {},
                                                     dispatcher);
          if (instId.valid()) {
            equipped.grantedEffectInstances.push_back(instId);
          }
        }
      }

      slots[slotKey] = static_cast<EquippedItem &&>(equipped);

      if (dispatcher) {
        dispatcher->emit(inventory_events::item_equipped,
                         SmallString{"inv.equipped"},
                         entityId, {},
                         static_cast<double>(itemInstanceId.value));
      }

      return true;
    }

    /// Unequip an item from an entity's slot. Returns the item instance ID
    /// that was unequipped, or an invalid ID if the slot was empty.
    ///
    /// On success:
    ///   - Stat modifiers from the item are removed
    ///   - Equipment-granted effects are removed
    ///   - An unequip event is emitted
    ItemInstanceId unequip(GameplayEntityId entityId, EquipmentSlot slot,
                           GameplayEventDispatcher *dispatcher = nullptr) {
      auto it = _entityEquipment.find(entityId.value);
      if (it == _entityEquipment.end()) return ItemInstanceId{0};

      auto slotKey = static_cast<u8>(slot);
      auto slotIt = it->second.find(slotKey);
      if (slotIt == it->second.end()) return ItemInstanceId{0};

      auto &equipped = slotIt->second;
      auto itemId = equipped.itemInstanceId;

      // Remove stat modifiers
      if (_effectSystem && equipped.syntheticEffectId.valid()) {
        auto *statBlock = _effectSystem->find_stat_block(entityId);
        if (statBlock) {
          // find_stat_block returns const; we need mutable access
          auto &mutableBlock = _effectSystem->stat_block(entityId);
          mutableBlock.remove_modifiers_from(equipped.syntheticEffectId);
        }
      }

      // Remove equipment-granted effects
      if (_effectSystem) {
        for (const auto &effectInstId : equipped.grantedEffectInstances) {
          _effectSystem->remove_effect(entityId, effectInstId, dispatcher);
        }
      }

      it->second.erase(slotIt);
      if (it->second.empty()) {
        _entityEquipment.erase(it);
      }

      if (dispatcher) {
        dispatcher->emit(inventory_events::item_unequipped,
                         SmallString{"inv.unequipped"},
                         entityId, {},
                         static_cast<double>(itemId.value));
      }

      return itemId;
    }

    /// Swap an equipped item: unequip old, equip new. Returns the old
    /// item instance ID, or invalid if unequip failed.
    ItemInstanceId swap_equipment(GameplayEntityId entityId,
                                  ItemInstanceId newItemInstanceId,
                                  const ItemDescriptor &newDesc,
                                  GameplayEventDispatcher *dispatcher = nullptr) {
      auto oldId = unequip(entityId, newDesc.slot, dispatcher);
      equip(entityId, newItemInstanceId, newDesc, dispatcher);
      return oldId;
    }

    // ---- Queries ----

    /// Get the item instance ID equipped in a slot. Invalid if empty.
    ItemInstanceId equipped_in(GameplayEntityId entityId,
                               EquipmentSlot slot) const noexcept {
      auto it = _entityEquipment.find(entityId.value);
      if (it == _entityEquipment.end()) return ItemInstanceId{0};
      auto slotIt = it->second.find(static_cast<u8>(slot));
      if (slotIt == it->second.end()) return ItemInstanceId{0};
      return slotIt->second.itemInstanceId;
    }

    /// Get the descriptor ID of the item equipped in a slot.
    ItemDescriptorId equipped_descriptor_in(GameplayEntityId entityId,
                                             EquipmentSlot slot) const noexcept {
      auto it = _entityEquipment.find(entityId.value);
      if (it == _entityEquipment.end()) return ItemDescriptorId{0};
      auto slotIt = it->second.find(static_cast<u8>(slot));
      if (slotIt == it->second.end()) return ItemDescriptorId{0};
      return slotIt->second.descriptorId;
    }

    /// Check if a slot is occupied.
    bool is_slot_occupied(GameplayEntityId entityId,
                          EquipmentSlot slot) const noexcept {
      return equipped_in(entityId, slot).valid();
    }

    /// Count how many slots an entity has equipped.
    size_t equipped_count(GameplayEntityId entityId) const noexcept {
      auto it = _entityEquipment.find(entityId.value);
      return it != _entityEquipment.end() ? it->second.size() : 0;
    }

    /// Unequip all items from an entity. Returns the count of items unequipped.
    size_t unequip_all(GameplayEntityId entityId,
                       GameplayEventDispatcher *dispatcher = nullptr) {
      auto it = _entityEquipment.find(entityId.value);
      if (it == _entityEquipment.end()) return 0;

      // Collect slot keys first to avoid iterator invalidation
      std::vector<EquipmentSlot> slots{};
      for (const auto &entry : it->second) {
        slots.push_back(static_cast<EquipmentSlot>(entry.first));
      }

      size_t count = 0;
      for (auto slot : slots) {
        if (unequip(entityId, slot, dispatcher).valid()) ++count;
      }
      return count;
    }

    /// Clear all equipment data.
    void clear() {
      _entityEquipment.clear();
      _descriptors.clear();
    }

  private:
    /// Tracks one equipped item in a slot.
    struct EquippedItem {
      ItemInstanceId itemInstanceId{};
      ItemDescriptorId descriptorId{};
      EquipmentSlot slot{EquipmentSlot::none};
      EffectInstanceId syntheticEffectId{};  ///< For stat modifier removal
      std::vector<EffectInstanceId> grantedEffectInstances{};
    };

    std::unordered_map<u64, ItemDescriptor> _descriptors{};
    /// Per-entity: slot key (u8) -> EquippedItem
    std::unordered_map<u64, std::unordered_map<u8, EquippedItem>> _entityEquipment{};
    EffectSystem *_effectSystem{nullptr};
  };

}  // namespace zs
