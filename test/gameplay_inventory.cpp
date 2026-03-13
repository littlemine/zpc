/// M7: Inventory And Equipment Hooks - Unit Tests
///
/// Tests the Inventory container, EquipmentSystem, ItemDescriptor data-driven
/// loading, stat modifier integration, equipment-granted effects, and events.

#include <cassert>
#include <cmath>
#include <cstdio>

#include "zensim/gameplay/GameplayInventory.hpp"

/// Helper: approximate floating-point equality.
static bool approx(double a, double b, double eps = 0.001) {
  return std::fabs(a - b) < eps;
}

/// Shared instance ID counter for tests.
static zs::u64 g_nextInstanceId = 0;

/// ---- Test 1: ItemDescriptorId / ItemInstanceId ----
static void test_id_types() {
  fprintf(stderr, "[inventory] test_id_types...\n");

  zs::ItemDescriptorId d0{};
  assert(!d0.valid());
  zs::ItemDescriptorId d1{1};
  assert(d1.valid());
  assert(d0 != d1);

  zs::ItemInstanceId i0{};
  assert(!i0.valid());
  zs::ItemInstanceId i1{1};
  assert(i1.valid());
  assert(i0 != i1);
  assert(i1 == zs::ItemInstanceId{1});

  fprintf(stderr, "[inventory] test_id_types PASS\n");
}

/// ---- Test 2: Enum names ----
static void test_enum_names() {
  fprintf(stderr, "[inventory] test_enum_names...\n");

  assert(item_rarity_name(zs::ItemRarity::common) != nullptr);
  assert(item_rarity_name(zs::ItemRarity::uncommon) != nullptr);
  assert(item_rarity_name(zs::ItemRarity::rare) != nullptr);
  assert(item_rarity_name(zs::ItemRarity::epic) != nullptr);
  assert(item_rarity_name(zs::ItemRarity::legendary) != nullptr);

  assert(equipment_slot_name(zs::EquipmentSlot::none) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::head) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::chest) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::legs) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::feet) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::hands) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::main_hand) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::off_hand) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::ring) != nullptr);
  assert(equipment_slot_name(zs::EquipmentSlot::amulet) != nullptr);

  fprintf(stderr, "[inventory] test_enum_names PASS\n");
}

/// ---- Test 3: equipment_slot_from_string ----
static void test_slot_from_string() {
  fprintf(stderr, "[inventory] test_slot_from_string...\n");

  assert(equipment_slot_from_string(zs::SmallString{"head"}) == zs::EquipmentSlot::head);
  assert(equipment_slot_from_string(zs::SmallString{"chest"}) == zs::EquipmentSlot::chest);
  assert(equipment_slot_from_string(zs::SmallString{"legs"}) == zs::EquipmentSlot::legs);
  assert(equipment_slot_from_string(zs::SmallString{"feet"}) == zs::EquipmentSlot::feet);
  assert(equipment_slot_from_string(zs::SmallString{"hands"}) == zs::EquipmentSlot::hands);
  assert(equipment_slot_from_string(zs::SmallString{"main_hand"}) == zs::EquipmentSlot::main_hand);
  assert(equipment_slot_from_string(zs::SmallString{"off_hand"}) == zs::EquipmentSlot::off_hand);
  assert(equipment_slot_from_string(zs::SmallString{"ring"}) == zs::EquipmentSlot::ring);
  assert(equipment_slot_from_string(zs::SmallString{"amulet"}) == zs::EquipmentSlot::amulet);
  assert(equipment_slot_from_string(zs::SmallString{"invalid"}) == zs::EquipmentSlot::none);
  assert(equipment_slot_from_string(zs::SmallString{}) == zs::EquipmentSlot::none);

  fprintf(stderr, "[inventory] test_slot_from_string PASS\n");
}

/// ---- Test 4: ItemDescriptor basic construction ----
static void test_item_descriptor_basic() {
  fprintf(stderr, "[inventory] test_item_descriptor_basic...\n");

  zs::ItemDescriptor desc{};
  desc.id = zs::ItemDescriptorId{1};
  desc.name = zs::SmallString{"Iron Sword"};
  desc.category = zs::SmallString{"weapon"};
  desc.rarity = zs::ItemRarity::uncommon;
  desc.slot = zs::EquipmentSlot::main_hand;
  desc.maxStackSize = 1;
  desc.weight = 3.5;
  desc.value = 100.0;

  assert(desc.is_equippable());
  assert(!desc.is_stackable());

  // Non-equippable item
  zs::ItemDescriptor potion{};
  potion.slot = zs::EquipmentSlot::none;
  potion.maxStackSize = 10;
  assert(!potion.is_equippable());
  assert(potion.is_stackable());

  fprintf(stderr, "[inventory] test_item_descriptor_basic PASS\n");
}

/// ---- Test 5: ItemDescriptor::from_definition ----
static void test_item_from_definition() {
  fprintf(stderr, "[inventory] test_item_from_definition...\n");

  zs::MechanicsDefinition def{};
  def.id = zs::SmallString{"healing_potion"};
  def.set_field(zs::SmallString{"name"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"Healing Potion"}));
  def.set_field(zs::SmallString{"category"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"consumable"}));
  def.set_field(zs::SmallString{"max_stack"}, zs::MechanicsFieldValue::make_integer(20));
  def.set_field(zs::SmallString{"weight"}, zs::MechanicsFieldValue::make_number(0.5));
  def.set_field(zs::SmallString{"value"}, zs::MechanicsFieldValue::make_number(25.0));
  def.set_field(zs::SmallString{"rarity"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"uncommon"}));
  def.set_field(zs::SmallString{"slot"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"none"}));

  auto desc = zs::ItemDescriptor::from_definition(def, zs::ItemDescriptorId{10});
  assert(desc.id.value == 10);
  assert(desc.name == zs::SmallString{"Healing Potion"});
  assert(desc.category == zs::SmallString{"consumable"});
  assert(desc.maxStackSize == 20);
  assert(approx(desc.weight, 0.5));
  assert(approx(desc.value, 25.0));
  assert(desc.rarity == zs::ItemRarity::uncommon);
  assert(desc.slot == zs::EquipmentSlot::none);
  assert(!desc.is_equippable());
  assert(desc.is_stackable());

  fprintf(stderr, "[inventory] test_item_from_definition PASS\n");
}

/// ---- Test 6: ItemDescriptor::from_definition with equipment slot ----
static void test_item_from_definition_equippable() {
  fprintf(stderr, "[inventory] test_item_from_definition_equippable...\n");

  zs::MechanicsDefinition def{};
  def.id = zs::SmallString{"steel_helm"};
  def.set_field(zs::SmallString{"name"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"Steel Helm"}));
  def.set_field(zs::SmallString{"slot"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"head"}));
  def.set_field(zs::SmallString{"rarity"}, zs::MechanicsFieldValue::make_string(zs::SmallString{"rare"}));

  auto desc = zs::ItemDescriptor::from_definition(def, zs::ItemDescriptorId{11});
  assert(desc.slot == zs::EquipmentSlot::head);
  assert(desc.rarity == zs::ItemRarity::rare);
  assert(desc.is_equippable());
  assert(!desc.is_stackable());  // maxStackSize defaults to 1

  fprintf(stderr, "[inventory] test_item_from_definition_equippable PASS\n");
}

/// ---- Test 7: Inventory capacity and empty state ----
static void test_inventory_capacity() {
  fprintf(stderr, "[inventory] test_inventory_capacity...\n");

  zs::Inventory inv{5};
  assert(inv.capacity() == 5);
  assert(inv.used_slots() == 0);
  assert(inv.free_slots() == 5);
  assert(!inv.is_full());
  assert(inv.is_empty());
  assert(inv.total_item_count() == 0);

  inv.set_capacity(10);
  assert(inv.capacity() == 10);

  fprintf(stderr, "[inventory] test_inventory_capacity PASS\n");
}

/// ---- Test 8: Inventory add non-stackable items ----
static void test_inventory_add_non_stackable() {
  fprintf(stderr, "[inventory] test_inventory_add_non_stackable...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{3};

  zs::ItemDescriptor sword{};
  sword.id = zs::ItemDescriptorId{1};
  sword.maxStackSize = 1;

  auto id1 = inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  assert(id1.valid());
  assert(inv.used_slots() == 1);
  assert(inv.total_item_count() == 1);

  auto id2 = inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  assert(id2.valid());
  assert(id2 != id1);
  assert(inv.used_slots() == 2);

  auto id3 = inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  assert(id3.valid());
  assert(inv.used_slots() == 3);
  assert(inv.is_full());

  // Inventory full: add should fail
  auto id4 = inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  assert(!id4.valid());
  assert(inv.used_slots() == 3);

  fprintf(stderr, "[inventory] test_inventory_add_non_stackable PASS\n");
}

/// ---- Test 9: Inventory add stackable items ----
static void test_inventory_add_stackable() {
  fprintf(stderr, "[inventory] test_inventory_add_stackable...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{5};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{2};
  potion.maxStackSize = 10;

  // Add 5 potions
  auto id1 = inv.add_item(zs::ItemDescriptorId{2}, 5, &potion, g_nextInstanceId);
  assert(id1.valid());
  assert(inv.used_slots() == 1);
  assert(inv.total_item_count() == 5);

  // Add 3 more: should stack
  auto id2 = inv.add_item(zs::ItemDescriptorId{2}, 3, &potion, g_nextInstanceId);
  assert(id2 == id1);  // Same stack
  assert(inv.used_slots() == 1);
  assert(inv.total_item_count() == 8);

  // Add 5 more: first 2 fill the existing stack, 3 go to new stack
  auto id3 = inv.add_item(zs::ItemDescriptorId{2}, 5, &potion, g_nextInstanceId);
  assert(id3.valid());
  assert(id3 != id1);  // New stack
  assert(inv.used_slots() == 2);
  assert(inv.total_item_count() == 13);

  // Verify counts
  assert(inv.count_item(zs::ItemDescriptorId{2}) == 13);
  assert(inv.has_item(zs::ItemDescriptorId{2}, 13));
  assert(!inv.has_item(zs::ItemDescriptorId{2}, 14));

  fprintf(stderr, "[inventory] test_inventory_add_stackable PASS\n");
}

/// ---- Test 10: Inventory remove by instance ID ----
static void test_inventory_remove_by_instance() {
  fprintf(stderr, "[inventory] test_inventory_remove_by_instance...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{10};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{2};
  potion.maxStackSize = 10;

  auto id = inv.add_item(zs::ItemDescriptorId{2}, 5, &potion, g_nextInstanceId);

  // Remove 3 from stack
  zs::u32 removed = inv.remove_item(id, 3);
  assert(removed == 3);
  assert(inv.total_item_count() == 2);
  assert(inv.used_slots() == 1);

  // Remove remaining 2 (stack should disappear)
  removed = inv.remove_item(id, 5);  // Over-remove: only 2 left
  assert(removed == 2);
  assert(inv.total_item_count() == 0);
  assert(inv.used_slots() == 0);
  assert(inv.is_empty());

  // Remove from non-existent
  removed = inv.remove_item(zs::ItemInstanceId{999}, 1);
  assert(removed == 0);

  fprintf(stderr, "[inventory] test_inventory_remove_by_instance PASS\n");
}

/// ---- Test 11: Inventory remove by descriptor ----
static void test_inventory_remove_by_descriptor() {
  fprintf(stderr, "[inventory] test_inventory_remove_by_descriptor...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{10};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{2};
  potion.maxStackSize = 5;

  // Add 12 potions: 5+5+2 = 3 stacks
  inv.add_item(zs::ItemDescriptorId{2}, 12, &potion, g_nextInstanceId);
  assert(inv.used_slots() == 3);
  assert(inv.total_item_count() == 12);

  // Remove 7 by descriptor
  zs::u32 removed = inv.remove_item_by_descriptor(zs::ItemDescriptorId{2}, 7);
  assert(removed == 7);
  assert(inv.total_item_count() == 5);
  // First stack consumed (5), second partially consumed (2), so 2 stacks left
  assert(inv.used_slots() == 2);

  // Remove all remaining
  removed = inv.remove_item_by_descriptor(zs::ItemDescriptorId{2}, 100);
  assert(removed == 5);
  assert(inv.is_empty());

  // Remove non-existent
  removed = inv.remove_item_by_descriptor(zs::ItemDescriptorId{999}, 1);
  assert(removed == 0);

  fprintf(stderr, "[inventory] test_inventory_remove_by_descriptor PASS\n");
}

/// ---- Test 12: Inventory find and query ----
static void test_inventory_queries() {
  fprintf(stderr, "[inventory] test_inventory_queries...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{10};

  zs::ItemDescriptor sword{};
  sword.id = zs::ItemDescriptorId{1};
  sword.maxStackSize = 1;

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{2};
  potion.maxStackSize = 10;

  auto swordId = inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  inv.add_item(zs::ItemDescriptorId{2}, 5, &potion, g_nextInstanceId);

  // find_item
  auto *found = inv.find_item(swordId);
  assert(found != nullptr);
  assert(found->descriptorId == zs::ItemDescriptorId{1});

  auto *notFound = inv.find_item(zs::ItemInstanceId{999});
  assert(notFound == nullptr);

  // items_of
  auto swords = inv.items_of(zs::ItemDescriptorId{1});
  assert(swords.size() == 1);

  auto potions = inv.items_of(zs::ItemDescriptorId{2});
  assert(potions.size() == 1);
  assert(potions[0]->stackCount == 5);

  // has_item
  assert(inv.has_item(zs::ItemDescriptorId{1}));
  assert(inv.has_item(zs::ItemDescriptorId{2}, 5));
  assert(!inv.has_item(zs::ItemDescriptorId{3}));

  // items()
  assert(inv.items().size() == 2);

  fprintf(stderr, "[inventory] test_inventory_queries PASS\n");
}

/// ---- Test 13: Inventory split stack ----
static void test_inventory_split_stack() {
  fprintf(stderr, "[inventory] test_inventory_split_stack...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{5};

  zs::ItemDescriptor arrows{};
  arrows.id = zs::ItemDescriptorId{3};
  arrows.maxStackSize = 64;

  auto id = inv.add_item(zs::ItemDescriptorId{3}, 30, &arrows, g_nextInstanceId);
  assert(inv.used_slots() == 1);
  assert(inv.total_item_count() == 30);

  // Split 10 off
  auto newId = inv.split_stack(id, 10, g_nextInstanceId);
  assert(newId.valid());
  assert(newId != id);
  assert(inv.used_slots() == 2);
  assert(inv.total_item_count() == 30);  // Total unchanged

  auto *original = inv.find_item(id);
  assert(original != nullptr);
  assert(original->stackCount == 20);

  auto *newStack = inv.find_item(newId);
  assert(newStack != nullptr);
  assert(newStack->stackCount == 10);

  // Invalid split: count >= stackCount
  auto badSplit = inv.split_stack(id, 20, g_nextInstanceId);
  assert(!badSplit.valid());

  // Invalid split: count = 0
  badSplit = inv.split_stack(id, 0, g_nextInstanceId);
  assert(!badSplit.valid());

  // Invalid split: non-existent item
  badSplit = inv.split_stack(zs::ItemInstanceId{999}, 5, g_nextInstanceId);
  assert(!badSplit.valid());

  fprintf(stderr, "[inventory] test_inventory_split_stack PASS\n");
}

/// ---- Test 14: Inventory events ----
static void test_inventory_events() {
  fprintf(stderr, "[inventory] test_inventory_events...\n");
  g_nextInstanceId = 0;

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(50);

  zs::Inventory inv{2};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{2};
  potion.maxStackSize = 5;

  // Add: should emit item_added
  inv.add_item(zs::ItemDescriptorId{2}, 3, &potion, g_nextInstanceId, &dispatcher);

  bool foundAdded = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_added) foundAdded = true;
  }
  assert(foundAdded);

  // Add more to stack: should emit item_stacked
  inv.add_item(zs::ItemDescriptorId{2}, 2, &potion, g_nextInstanceId, &dispatcher);

  bool foundStacked = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_stacked) foundStacked = true;
  }
  assert(foundStacked);

  // Fill inventory and try to add more
  zs::ItemDescriptor sword{};
  sword.id = zs::ItemDescriptorId{1};
  sword.maxStackSize = 1;
  inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId, &dispatcher);
  assert(inv.is_full());

  inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId, &dispatcher);

  bool foundFull = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::inventory_full) foundFull = true;
  }
  assert(foundFull);

  fprintf(stderr, "[inventory] test_inventory_events PASS\n");
}

/// ---- Test 15: Inventory remove events ----
static void test_inventory_remove_events() {
  fprintf(stderr, "[inventory] test_inventory_remove_events...\n");
  g_nextInstanceId = 0;

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(20);

  zs::Inventory inv{5};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{2};
  potion.maxStackSize = 10;

  auto id = inv.add_item(zs::ItemDescriptorId{2}, 5, &potion, g_nextInstanceId);
  inv.remove_item(id, 2, &dispatcher);

  bool foundRemoved = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_removed) foundRemoved = true;
  }
  assert(foundRemoved);

  fprintf(stderr, "[inventory] test_inventory_remove_events PASS\n");
}

/// ---- Test 16: Inventory split events ----
static void test_inventory_split_events() {
  fprintf(stderr, "[inventory] test_inventory_split_events...\n");
  g_nextInstanceId = 0;

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(20);

  zs::Inventory inv{5};

  zs::ItemDescriptor arrows{};
  arrows.id = zs::ItemDescriptorId{3};
  arrows.maxStackSize = 64;

  auto id = inv.add_item(zs::ItemDescriptorId{3}, 20, &arrows, g_nextInstanceId);
  inv.split_stack(id, 5, g_nextInstanceId, &dispatcher);

  bool foundSplit = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_split) foundSplit = true;
  }
  assert(foundSplit);

  fprintf(stderr, "[inventory] test_inventory_split_events PASS\n");
}

/// ---- Test 17: Inventory clear ----
static void test_inventory_clear() {
  fprintf(stderr, "[inventory] test_inventory_clear...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{10};

  zs::ItemDescriptor sword{};
  sword.id = zs::ItemDescriptorId{1};
  sword.maxStackSize = 1;

  inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  inv.add_item(zs::ItemDescriptorId{1}, 1, &sword, g_nextInstanceId);
  assert(inv.used_slots() == 2);

  inv.clear();
  assert(inv.is_empty());
  assert(inv.used_slots() == 0);
  assert(inv.total_item_count() == 0);

  fprintf(stderr, "[inventory] test_inventory_clear PASS\n");
}

/// ---- Test 18: EquipmentSystem descriptor management ----
static void test_equipment_descriptor_management() {
  fprintf(stderr, "[inventory] test_equipment_descriptor_management...\n");

  zs::EquipmentSystem equipSys{};

  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{1};
  helm.name = zs::SmallString{"Iron Helm"};
  helm.slot = zs::EquipmentSlot::head;

  assert(equipSys.register_descriptor(helm));
  assert(equipSys.descriptor_count() == 1);
  assert(!equipSys.register_descriptor(helm));  // Duplicate

  auto *found = equipSys.find_descriptor(zs::ItemDescriptorId{1});
  assert(found != nullptr);
  assert(found->name == zs::SmallString{"Iron Helm"});

  assert(equipSys.find_descriptor(zs::ItemDescriptorId{999}) == nullptr);

  assert(equipSys.remove_descriptor(zs::ItemDescriptorId{1}));
  assert(equipSys.descriptor_count() == 0);

  // Invalid ID
  zs::ItemDescriptor invalid{};
  assert(!equipSys.register_descriptor(invalid));

  fprintf(stderr, "[inventory] test_equipment_descriptor_management PASS\n");
}

/// ---- Test 19: Basic equip / unequip ----
static void test_basic_equip_unequip() {
  fprintf(stderr, "[inventory] test_basic_equip_unequip...\n");

  zs::EquipmentSystem equipSys{};
  zs::GameplayEntityId entity{1};

  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{10};
  helm.name = zs::SmallString{"Iron Helm"};
  helm.slot = zs::EquipmentSlot::head;

  // Equip
  bool ok = equipSys.equip(entity, zs::ItemInstanceId{100}, helm);
  assert(ok);
  assert(equipSys.is_slot_occupied(entity, zs::EquipmentSlot::head));
  assert(equipSys.equipped_in(entity, zs::EquipmentSlot::head) == zs::ItemInstanceId{100});
  assert(equipSys.equipped_count(entity) == 1);

  // Can't equip same slot again
  ok = equipSys.equip(entity, zs::ItemInstanceId{101}, helm);
  assert(!ok);

  // Unequip
  auto removed = equipSys.unequip(entity, zs::EquipmentSlot::head);
  assert(removed == zs::ItemInstanceId{100});
  assert(!equipSys.is_slot_occupied(entity, zs::EquipmentSlot::head));
  assert(equipSys.equipped_count(entity) == 0);

  // Unequip empty slot
  removed = equipSys.unequip(entity, zs::EquipmentSlot::head);
  assert(!removed.valid());

  fprintf(stderr, "[inventory] test_basic_equip_unequip PASS\n");
}

/// ---- Test 20: Equip non-equippable item ----
static void test_equip_non_equippable() {
  fprintf(stderr, "[inventory] test_equip_non_equippable...\n");

  zs::EquipmentSystem equipSys{};
  zs::GameplayEntityId entity{1};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{20};
  potion.slot = zs::EquipmentSlot::none;

  bool ok = equipSys.equip(entity, zs::ItemInstanceId{200}, potion);
  assert(!ok);

  fprintf(stderr, "[inventory] test_equip_non_equippable PASS\n");
}

/// ---- Test 21: Equipment stat modifiers ----
static void test_equipment_stat_modifiers() {
  fprintf(stderr, "[inventory] test_equipment_stat_modifiers...\n");

  zs::EffectSystem effectSys{};
  auto &statBlock = effectSys.stat_block(zs::GameplayEntityId{1});
  statBlock.set_base(zs::SmallString{"armor"}, 10.0);
  statBlock.set_base(zs::SmallString{"strength"}, 20.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor plateChest{};
  plateChest.id = zs::ItemDescriptorId{30};
  plateChest.slot = zs::EquipmentSlot::chest;
  zs::StatModifier armorMod{};
  armorMod.statName = zs::SmallString{"armor"};
  armorMod.operation = zs::StatModOp::additive;
  armorMod.value = 50.0;
  plateChest.equipModifiers.push_back(armorMod);
  zs::StatModifier strMod{};
  strMod.statName = zs::SmallString{"strength"};
  strMod.operation = zs::StatModOp::additive;
  strMod.value = 5.0;
  plateChest.equipModifiers.push_back(strMod);

  // Before equip
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 10.0));
  assert(approx(statBlock.compute(zs::SmallString{"strength"}), 20.0));

  // Equip
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{300}, plateChest);

  // After equip: stats modified
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 60.0));
  assert(approx(statBlock.compute(zs::SmallString{"strength"}), 25.0));

  // Unequip: stats restored
  equipSys.unequip(zs::GameplayEntityId{1}, zs::EquipmentSlot::chest);
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 10.0));
  assert(approx(statBlock.compute(zs::SmallString{"strength"}), 20.0));

  fprintf(stderr, "[inventory] test_equipment_stat_modifiers PASS\n");
}

/// ---- Test 22: Equipment-granted effects ----
static void test_equipment_granted_effects() {
  fprintf(stderr, "[inventory] test_equipment_granted_effects...\n");

  zs::EffectSystem effectSys{};
  zs::GameplayEntityId entity{1};

  // Register a thorns effect
  zs::EffectDescriptor thorns{};
  thorns.id = zs::EffectDescriptorId{50};
  thorns.name = zs::SmallString{"Thorns"};
  thorns.durationType = zs::EffectDurationType::infinite;
  thorns.magnitude = 10.0;
  effectSys.register_descriptor(thorns);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor spikedShield{};
  spikedShield.id = zs::ItemDescriptorId{40};
  spikedShield.slot = zs::EquipmentSlot::off_hand;
  spikedShield.equipEffects.push_back(zs::EffectDescriptorId{50});

  // No effects before equip
  assert(effectSys.effect_count(entity) == 0);

  // Equip: effect should be applied
  equipSys.equip(entity, zs::ItemInstanceId{400}, spikedShield);
  assert(effectSys.effect_count(entity) == 1);
  assert(effectSys.has_effect(entity, zs::EffectDescriptorId{50}));

  // Unequip: effect should be removed
  equipSys.unequip(entity, zs::EquipmentSlot::off_hand);
  assert(effectSys.effect_count(entity) == 0);

  fprintf(stderr, "[inventory] test_equipment_granted_effects PASS\n");
}

/// ---- Test 23: Equipment events ----
static void test_equipment_events() {
  fprintf(stderr, "[inventory] test_equipment_events...\n");

  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(20);

  zs::EquipmentSystem equipSys{};
  zs::GameplayEntityId entity{1};

  zs::ItemDescriptor boots{};
  boots.id = zs::ItemDescriptorId{50};
  boots.slot = zs::EquipmentSlot::feet;

  equipSys.equip(entity, zs::ItemInstanceId{500}, boots, &dispatcher);

  bool foundEquipped = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_equipped) {
      foundEquipped = true;
      assert(evt.source == entity);
    }
  }
  assert(foundEquipped);

  equipSys.unequip(entity, zs::EquipmentSlot::feet, &dispatcher);

  bool foundUnequipped = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_unequipped) {
      foundUnequipped = true;
      assert(evt.source == entity);
    }
  }
  assert(foundUnequipped);

  fprintf(stderr, "[inventory] test_equipment_events PASS\n");
}

/// ---- Test 24: Equipment swap ----
static void test_equipment_swap() {
  fprintf(stderr, "[inventory] test_equipment_swap...\n");

  zs::EffectSystem effectSys{};
  auto &statBlock = effectSys.stat_block(zs::GameplayEntityId{1});
  statBlock.set_base(zs::SmallString{"armor"}, 0.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor oldHelm{};
  oldHelm.id = zs::ItemDescriptorId{60};
  oldHelm.slot = zs::EquipmentSlot::head;
  zs::StatModifier oldMod{};
  oldMod.statName = zs::SmallString{"armor"};
  oldMod.operation = zs::StatModOp::additive;
  oldMod.value = 10.0;
  oldHelm.equipModifiers.push_back(oldMod);

  zs::ItemDescriptor newHelm{};
  newHelm.id = zs::ItemDescriptorId{61};
  newHelm.slot = zs::EquipmentSlot::head;
  zs::StatModifier newMod{};
  newMod.statName = zs::SmallString{"armor"};
  newMod.operation = zs::StatModOp::additive;
  newMod.value = 25.0;
  newHelm.equipModifiers.push_back(newMod);

  // Equip old helm
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{600}, oldHelm);
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 10.0));

  // Swap to new helm
  auto oldId = equipSys.swap_equipment(zs::GameplayEntityId{1},
                                        zs::ItemInstanceId{601}, newHelm);
  assert(oldId == zs::ItemInstanceId{600});
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 25.0));
  assert(equipSys.equipped_in(zs::GameplayEntityId{1}, zs::EquipmentSlot::head)
         == zs::ItemInstanceId{601});

  fprintf(stderr, "[inventory] test_equipment_swap PASS\n");
}

/// ---- Test 25: Equipment queries ----
static void test_equipment_queries() {
  fprintf(stderr, "[inventory] test_equipment_queries...\n");

  zs::EquipmentSystem equipSys{};
  zs::GameplayEntityId entity{1};

  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{70};
  helm.slot = zs::EquipmentSlot::head;

  zs::ItemDescriptor chest{};
  chest.id = zs::ItemDescriptorId{71};
  chest.slot = zs::EquipmentSlot::chest;

  equipSys.equip(entity, zs::ItemInstanceId{700}, helm);
  equipSys.equip(entity, zs::ItemInstanceId{701}, chest);

  assert(equipSys.equipped_count(entity) == 2);
  assert(equipSys.is_slot_occupied(entity, zs::EquipmentSlot::head));
  assert(equipSys.is_slot_occupied(entity, zs::EquipmentSlot::chest));
  assert(!equipSys.is_slot_occupied(entity, zs::EquipmentSlot::legs));

  assert(equipSys.equipped_descriptor_in(entity, zs::EquipmentSlot::head)
         == zs::ItemDescriptorId{70});
  assert(equipSys.equipped_descriptor_in(entity, zs::EquipmentSlot::chest)
         == zs::ItemDescriptorId{71});

  // Non-existent entity
  assert(equipSys.equipped_count(zs::GameplayEntityId{999}) == 0);
  assert(!equipSys.equipped_in(zs::GameplayEntityId{999}, zs::EquipmentSlot::head).valid());

  fprintf(stderr, "[inventory] test_equipment_queries PASS\n");
}

/// ---- Test 26: Unequip all ----
static void test_unequip_all() {
  fprintf(stderr, "[inventory] test_unequip_all...\n");

  zs::EffectSystem effectSys{};
  auto &statBlock = effectSys.stat_block(zs::GameplayEntityId{1});
  statBlock.set_base(zs::SmallString{"armor"}, 0.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{80};
  helm.slot = zs::EquipmentSlot::head;
  zs::StatModifier mod1{};
  mod1.statName = zs::SmallString{"armor"};
  mod1.operation = zs::StatModOp::additive;
  mod1.value = 10.0;
  helm.equipModifiers.push_back(mod1);

  zs::ItemDescriptor chest{};
  chest.id = zs::ItemDescriptorId{81};
  chest.slot = zs::EquipmentSlot::chest;
  zs::StatModifier mod2{};
  mod2.statName = zs::SmallString{"armor"};
  mod2.operation = zs::StatModOp::additive;
  mod2.value = 30.0;
  chest.equipModifiers.push_back(mod2);

  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{800}, helm);
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{801}, chest);
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 40.0));

  size_t count = equipSys.unequip_all(zs::GameplayEntityId{1});
  assert(count == 2);
  assert(equipSys.equipped_count(zs::GameplayEntityId{1}) == 0);
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 0.0));

  // Unequip all on empty
  count = equipSys.unequip_all(zs::GameplayEntityId{1});
  assert(count == 0);

  fprintf(stderr, "[inventory] test_unequip_all PASS\n");
}

/// ---- Test 27: Equipment system clear ----
static void test_equipment_system_clear() {
  fprintf(stderr, "[inventory] test_equipment_system_clear...\n");

  zs::EquipmentSystem equipSys{};

  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{90};
  helm.slot = zs::EquipmentSlot::head;
  equipSys.register_descriptor(helm);
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{900}, helm);

  assert(equipSys.descriptor_count() == 1);
  assert(equipSys.equipped_count(zs::GameplayEntityId{1}) == 1);

  equipSys.clear();
  assert(equipSys.descriptor_count() == 0);
  assert(equipSys.equipped_count(zs::GameplayEntityId{1}) == 0);

  fprintf(stderr, "[inventory] test_equipment_system_clear PASS\n");
}

/// ---- Test 28: Multiple equipment slots on one entity ----
static void test_multiple_slots() {
  fprintf(stderr, "[inventory] test_multiple_slots...\n");

  zs::EffectSystem effectSys{};
  auto &statBlock = effectSys.stat_block(zs::GameplayEntityId{1});
  statBlock.set_base(zs::SmallString{"armor"}, 0.0);
  statBlock.set_base(zs::SmallString{"attack_power"}, 10.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  // Create items for multiple slots
  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{100};
  helm.slot = zs::EquipmentSlot::head;
  zs::StatModifier helmMod{};
  helmMod.statName = zs::SmallString{"armor"};
  helmMod.operation = zs::StatModOp::additive;
  helmMod.value = 15.0;
  helm.equipModifiers.push_back(helmMod);

  zs::ItemDescriptor sword{};
  sword.id = zs::ItemDescriptorId{101};
  sword.slot = zs::EquipmentSlot::main_hand;
  zs::StatModifier swordMod{};
  swordMod.statName = zs::SmallString{"attack_power"};
  swordMod.operation = zs::StatModOp::additive;
  swordMod.value = 25.0;
  sword.equipModifiers.push_back(swordMod);

  zs::ItemDescriptor ring{};
  ring.id = zs::ItemDescriptorId{102};
  ring.slot = zs::EquipmentSlot::ring;
  zs::StatModifier ringArmor{};
  ringArmor.statName = zs::SmallString{"armor"};
  ringArmor.operation = zs::StatModOp::additive;
  ringArmor.value = 5.0;
  ring.equipModifiers.push_back(ringArmor);
  zs::StatModifier ringAtk{};
  ringAtk.statName = zs::SmallString{"attack_power"};
  ringAtk.operation = zs::StatModOp::additive;
  ringAtk.value = 3.0;
  ring.equipModifiers.push_back(ringAtk);

  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{1000}, helm);
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{1001}, sword);
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{1002}, ring);

  assert(equipSys.equipped_count(zs::GameplayEntityId{1}) == 3);
  // armor: 0 + 15 (helm) + 5 (ring) = 20
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 20.0));
  // attack_power: 10 + 25 (sword) + 3 (ring) = 38
  assert(approx(statBlock.compute(zs::SmallString{"attack_power"}), 38.0));

  // Unequip ring: armor = 15, attack_power = 35
  equipSys.unequip(zs::GameplayEntityId{1}, zs::EquipmentSlot::ring);
  assert(approx(statBlock.compute(zs::SmallString{"armor"}), 15.0));
  assert(approx(statBlock.compute(zs::SmallString{"attack_power"}), 35.0));

  fprintf(stderr, "[inventory] test_multiple_slots PASS\n");
}

/// ---- Test 29: Multiple entities with equipment ----
static void test_multiple_entities_equipment() {
  fprintf(stderr, "[inventory] test_multiple_entities_equipment...\n");

  zs::EffectSystem effectSys{};
  auto &stats1 = effectSys.stat_block(zs::GameplayEntityId{1});
  stats1.set_base(zs::SmallString{"armor"}, 0.0);
  auto &stats2 = effectSys.stat_block(zs::GameplayEntityId{2});
  stats2.set_base(zs::SmallString{"armor"}, 0.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor helm{};
  helm.id = zs::ItemDescriptorId{110};
  helm.slot = zs::EquipmentSlot::head;
  zs::StatModifier mod{};
  mod.statName = zs::SmallString{"armor"};
  mod.operation = zs::StatModOp::additive;
  mod.value = 20.0;
  helm.equipModifiers.push_back(mod);

  // Equip on both entities (different instances)
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{1100}, helm);
  equipSys.equip(zs::GameplayEntityId{2}, zs::ItemInstanceId{1101}, helm);

  assert(approx(stats1.compute(zs::SmallString{"armor"}), 20.0));
  assert(approx(stats2.compute(zs::SmallString{"armor"}), 20.0));

  // Unequip from entity 1 only
  equipSys.unequip(zs::GameplayEntityId{1}, zs::EquipmentSlot::head);
  assert(approx(stats1.compute(zs::SmallString{"armor"}), 0.0));
  assert(approx(stats2.compute(zs::SmallString{"armor"}), 20.0));  // Unchanged

  fprintf(stderr, "[inventory] test_multiple_entities_equipment PASS\n");
}

/// ---- Test 30: Equipment with multiplicative modifier ----
static void test_equipment_multiplicative_modifier() {
  fprintf(stderr, "[inventory] test_equipment_multiplicative_modifier...\n");

  zs::EffectSystem effectSys{};
  auto &statBlock = effectSys.stat_block(zs::GameplayEntityId{1});
  statBlock.set_base(zs::SmallString{"attack_power"}, 100.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor ring{};
  ring.id = zs::ItemDescriptorId{120};
  ring.slot = zs::EquipmentSlot::ring;
  zs::StatModifier mult{};
  mult.statName = zs::SmallString{"attack_power"};
  mult.operation = zs::StatModOp::multiplicative;
  mult.value = 1.5;  // +50%
  ring.equipModifiers.push_back(mult);

  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{1200}, ring);
  // attack_power: 100 * 1.5 = 150
  assert(approx(statBlock.compute(zs::SmallString{"attack_power"}), 150.0));

  equipSys.unequip(zs::GameplayEntityId{1}, zs::EquipmentSlot::ring);
  assert(approx(statBlock.compute(zs::SmallString{"attack_power"}), 100.0));

  fprintf(stderr, "[inventory] test_equipment_multiplicative_modifier PASS\n");
}

/// ---- Test 31: Inventory + Equipment integrated scenario ----
static void test_integrated_inventory_equipment() {
  fprintf(stderr, "[inventory] test_integrated_inventory_equipment...\n");
  g_nextInstanceId = 0;

  zs::EffectSystem effectSys{};
  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(50);

  auto &stats = effectSys.stat_block(zs::GameplayEntityId{1});
  stats.set_base(zs::SmallString{"armor"}, 5.0);
  stats.set_base(zs::SmallString{"attack_power"}, 10.0);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::Inventory inv{20};

  // Create item descriptors
  zs::ItemDescriptor ironSword{};
  ironSword.id = zs::ItemDescriptorId{200};
  ironSword.name = zs::SmallString{"Iron Sword"};
  ironSword.slot = zs::EquipmentSlot::main_hand;
  ironSword.maxStackSize = 1;
  zs::StatModifier swordMod{};
  swordMod.statName = zs::SmallString{"attack_power"};
  swordMod.operation = zs::StatModOp::additive;
  swordMod.value = 15.0;
  ironSword.equipModifiers.push_back(swordMod);

  zs::ItemDescriptor healthPotion{};
  healthPotion.id = zs::ItemDescriptorId{201};
  healthPotion.name = zs::SmallString{"Health Potion"};
  healthPotion.maxStackSize = 10;
  healthPotion.slot = zs::EquipmentSlot::none;

  // Add items to inventory
  auto swordInstId = inv.add_item(zs::ItemDescriptorId{200}, 1, &ironSword,
                                   g_nextInstanceId, &dispatcher);
  inv.add_item(zs::ItemDescriptorId{201}, 5, &healthPotion,
               g_nextInstanceId, &dispatcher);

  assert(inv.used_slots() == 2);
  assert(inv.count_item(zs::ItemDescriptorId{201}) == 5);

  // Equip the sword from inventory
  equipSys.equip(zs::GameplayEntityId{1}, swordInstId, ironSword, &dispatcher);
  assert(approx(stats.compute(zs::SmallString{"attack_power"}), 25.0));

  // "Use" a potion (remove from inventory)
  zs::u32 used = inv.remove_item_by_descriptor(zs::ItemDescriptorId{201}, 1, &dispatcher);
  assert(used == 1);
  assert(inv.count_item(zs::ItemDescriptorId{201}) == 4);

  // Unequip sword
  equipSys.unequip(zs::GameplayEntityId{1}, zs::EquipmentSlot::main_hand, &dispatcher);
  assert(approx(stats.compute(zs::SmallString{"attack_power"}), 10.0));

  // Verify events were emitted
  bool foundEquip = false;
  bool foundUnequip = false;
  bool foundAdd = false;
  bool foundRemove = false;
  for (const auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::inventory_events::item_equipped) foundEquip = true;
    if (evt.typeId == zs::inventory_events::item_unequipped) foundUnequip = true;
    if (evt.typeId == zs::inventory_events::item_added) foundAdd = true;
    if (evt.typeId == zs::inventory_events::item_removed) foundRemove = true;
  }
  assert(foundEquip);
  assert(foundUnequip);
  assert(foundAdd);
  assert(foundRemove);

  fprintf(stderr, "[inventory] test_integrated_inventory_equipment PASS\n");
}

/// ---- Test 32: Inventory full with partial stacking ----
static void test_inventory_full_partial_stack() {
  fprintf(stderr, "[inventory] test_inventory_full_partial_stack...\n");
  g_nextInstanceId = 0;

  zs::Inventory inv{2};

  zs::ItemDescriptor potion{};
  potion.id = zs::ItemDescriptorId{300};
  potion.maxStackSize = 5;

  // Fill both slots with partial stacks
  inv.add_item(zs::ItemDescriptorId{300}, 3, &potion, g_nextInstanceId);
  assert(inv.used_slots() == 1);

  zs::ItemDescriptor sword{};
  sword.id = zs::ItemDescriptorId{301};
  sword.maxStackSize = 1;
  inv.add_item(zs::ItemDescriptorId{301}, 1, &sword, g_nextInstanceId);
  assert(inv.used_slots() == 2);
  assert(inv.is_full());

  // Can still add potions to existing stack
  auto id = inv.add_item(zs::ItemDescriptorId{300}, 2, &potion, g_nextInstanceId);
  assert(id.valid());
  assert(inv.count_item(zs::ItemDescriptorId{300}) == 5);  // 3 + 2
  assert(inv.used_slots() == 2);  // No new slot needed

  // Can't add more potions (stack full, inventory full)
  zs::GameplayEventDispatcher dispatcher{};
  dispatcher.set_history_capacity(10);
  id = inv.add_item(zs::ItemDescriptorId{300}, 1, &potion, g_nextInstanceId, &dispatcher);
  // Stack is at max (5), can't create new slot
  assert(!id.valid());

  fprintf(stderr, "[inventory] test_inventory_full_partial_stack PASS\n");
}

/// ---- Test 33: Equipment granted effects with stat modifiers ----
static void test_equipment_effect_with_stat_modifiers() {
  fprintf(stderr, "[inventory] test_equipment_effect_with_stat_modifiers...\n");

  zs::EffectSystem effectSys{};
  auto &statBlock = effectSys.stat_block(zs::GameplayEntityId{1});
  statBlock.set_base(zs::SmallString{"speed"}, 100.0);

  // Register a speed buff effect
  zs::EffectDescriptor speedBuff{};
  speedBuff.id = zs::EffectDescriptorId{60};
  speedBuff.name = zs::SmallString{"WindWalk"};
  speedBuff.durationType = zs::EffectDurationType::infinite;
  zs::StatModifier speedMod{};
  speedMod.statName = zs::SmallString{"speed"};
  speedMod.operation = zs::StatModOp::additive;
  speedMod.value = 30.0;
  speedBuff.modifiers.push_back(speedMod);
  effectSys.register_descriptor(speedBuff);

  zs::EquipmentSystem equipSys{};
  equipSys.set_effect_system(&effectSys);

  zs::ItemDescriptor boots{};
  boots.id = zs::ItemDescriptorId{400};
  boots.slot = zs::EquipmentSlot::feet;
  boots.equipEffects.push_back(zs::EffectDescriptorId{60});

  // Equip: speed buff should apply
  equipSys.equip(zs::GameplayEntityId{1}, zs::ItemInstanceId{4000}, boots);
  assert(approx(statBlock.compute(zs::SmallString{"speed"}), 130.0));

  // Unequip: speed should return to base
  equipSys.unequip(zs::GameplayEntityId{1}, zs::EquipmentSlot::feet);
  assert(approx(statBlock.compute(zs::SmallString{"speed"}), 100.0));

  fprintf(stderr, "[inventory] test_equipment_effect_with_stat_modifiers PASS\n");
}

/// ---- Test 34: Item rarity from definition ----
static void test_item_rarity_from_definition() {
  fprintf(stderr, "[inventory] test_item_rarity_from_definition...\n");

  auto test_rarity = [](const char *rarityStr, zs::ItemRarity expected) {
    zs::MechanicsDefinition def{};
    def.id = zs::SmallString{"test_item"};
    def.set_field(zs::SmallString{"rarity"}, zs::MechanicsFieldValue::make_string(zs::SmallString{rarityStr}));
    auto desc = zs::ItemDescriptor::from_definition(def, zs::ItemDescriptorId{1});
    assert(desc.rarity == expected);
  };

  test_rarity("common", zs::ItemRarity::common);
  test_rarity("uncommon", zs::ItemRarity::uncommon);
  test_rarity("rare", zs::ItemRarity::rare);
  test_rarity("epic", zs::ItemRarity::epic);
  test_rarity("legendary", zs::ItemRarity::legendary);
  test_rarity("invalid", zs::ItemRarity::common);  // Default

  fprintf(stderr, "[inventory] test_item_rarity_from_definition PASS\n");
}

/// ---- Test 35: Default capacity ----
static void test_default_capacity() {
  fprintf(stderr, "[inventory] test_default_capacity...\n");

  zs::Inventory inv{};
  assert(inv.capacity() == 20);  // Default
  assert(inv.is_empty());

  fprintf(stderr, "[inventory] test_default_capacity PASS\n");
}

int main() {
  fprintf(stderr, "===========================================\n");
  fprintf(stderr, "M7: Inventory And Equipment Hooks Tests\n");
  fprintf(stderr, "===========================================\n\n");

  test_id_types();                               // 1
  test_enum_names();                             // 2
  test_slot_from_string();                       // 3
  test_item_descriptor_basic();                  // 4
  test_item_from_definition();                   // 5
  test_item_from_definition_equippable();        // 6
  test_inventory_capacity();                     // 7
  test_inventory_add_non_stackable();            // 8
  test_inventory_add_stackable();                // 9
  test_inventory_remove_by_instance();           // 10
  test_inventory_remove_by_descriptor();         // 11
  test_inventory_queries();                      // 12
  test_inventory_split_stack();                  // 13
  test_inventory_events();                       // 14
  test_inventory_remove_events();                // 15
  test_inventory_split_events();                 // 16
  test_inventory_clear();                        // 17
  test_equipment_descriptor_management();        // 18
  test_basic_equip_unequip();                    // 19
  test_equip_non_equippable();                   // 20
  test_equipment_stat_modifiers();               // 21
  test_equipment_granted_effects();              // 22
  test_equipment_events();                       // 23
  test_equipment_swap();                         // 24
  test_equipment_queries();                      // 25
  test_unequip_all();                            // 26
  test_equipment_system_clear();                 // 27
  test_multiple_slots();                         // 28
  test_multiple_entities_equipment();            // 29
  test_equipment_multiplicative_modifier();      // 30
  test_integrated_inventory_equipment();         // 31
  test_inventory_full_partial_stack();           // 32
  test_equipment_effect_with_stat_modifiers();   // 33
  test_item_rarity_from_definition();            // 34
  test_default_capacity();                       // 35

  fprintf(stderr, "\n===========================================\n");
  fprintf(stderr, "All 35 inventory/equipment tests PASSED\n");
  fprintf(stderr, "===========================================\n");

  return 0;
}
