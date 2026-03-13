#include <cassert>
#include <cstdio>

#include "zensim/gameplay/GameplayEvent.hpp"

// A concrete test component.
struct HealthComponent : zs::GameplayComponent {
  static constexpr zs::GameplayComponentTypeId staticTypeId() {
    return zs::GameplayComponentTypeId{1};
  }
  zs::GameplayComponentTypeId type_id() const noexcept override { return staticTypeId(); }
  const char *type_name() const noexcept override { return "HealthComponent"; }

  double maxHp{100.0};
  double currentHp{100.0};
};

struct ManaComponent : zs::GameplayComponent {
  static constexpr zs::GameplayComponentTypeId staticTypeId() {
    return zs::GameplayComponentTypeId{2};
  }
  zs::GameplayComponentTypeId type_id() const noexcept override { return staticTypeId(); }
  const char *type_name() const noexcept override { return "ManaComponent"; }

  double maxMana{50.0};
  double currentMana{50.0};
};

int main() {
  using namespace zs;

  // ====== GameplayTag tests ======
  {
    GameplayTag empty{};
    assert(empty.empty());

    GameplayTag status{"status"};
    GameplayTag burning{"status.burning"};
    GameplayTag strongBurning{"status.burning.strong"};
    GameplayTag frozen{"status.frozen"};

    assert(!status.empty());
    assert(status == GameplayTag{"status"});
    assert(status != burning);

    // Hierarchy
    assert(burning.is_child_of(status));
    assert(strongBurning.is_child_of(status));
    assert(strongBurning.is_child_of(burning));
    assert(!status.is_child_of(burning));
    assert(!frozen.is_child_of(burning));
    // A tag is a child of itself.
    assert(status.is_child_of(status));

    // Ordering (lexicographic via strcmp)
    assert(burning < frozen);
    assert(!(frozen < burning));

    fprintf(stderr, "[PASS] GameplayTag\n");
  }

  // ====== GameplayTagContainer tests ======
  {
    GameplayTagContainer container{};
    assert(container.empty());
    assert(container.size() == 0);

    // Add
    assert(container.add(GameplayTag{"status.burning"}));
    assert(container.add(GameplayTag{"status.frozen"}));
    assert(container.add(GameplayTag{"ability.fireball"}));
    assert(container.size() == 3);

    // Duplicate add
    assert(!container.add(GameplayTag{"status.burning"}));
    assert(container.size() == 3);

    // Has
    assert(container.has(GameplayTag{"status.burning"}));
    assert(container.has(GameplayTag{"status.frozen"}));
    assert(!container.has(GameplayTag{"status"}));

    // has_any_child_of
    assert(container.has_any_child_of(GameplayTag{"status"}));
    assert(container.has_any_child_of(GameplayTag{"ability"}));
    assert(!container.has_any_child_of(GameplayTag{"movement"}));

    // children_of
    auto statusChildren = container.children_of(GameplayTag{"status"});
    assert(statusChildren.size() == 2);

    // Remove
    assert(container.remove(GameplayTag{"status.frozen"}));
    assert(!container.has(GameplayTag{"status.frozen"}));
    assert(container.size() == 2);

    // Remove non-existent
    assert(!container.remove(GameplayTag{"status.frozen"}));

    // remove_children_of
    container.add(GameplayTag{"status.burning.strong"});
    assert(container.size() == 3);
    size_t removed = container.remove_children_of(GameplayTag{"status.burning"});
    assert(removed == 2); // "status.burning" and "status.burning.strong"
    assert(container.size() == 1);
    assert(container.has(GameplayTag{"ability.fireball"}));

    // has_all / has_any
    GameplayTagContainer required{};
    required.add(GameplayTag{"ability.fireball"});
    assert(container.has_all(required));

    required.add(GameplayTag{"status.burning"});
    assert(!container.has_all(required));
    assert(container.has_any(required));

    GameplayTagContainer unrelated{};
    unrelated.add(GameplayTag{"movement.sprint"});
    assert(!container.has_any(unrelated));

    // Clear
    container.clear();
    assert(container.empty());

    fprintf(stderr, "[PASS] GameplayTagContainer\n");
  }

  // ====== GameplayEntity tests ======
  {
    GameplayEntity entity{GameplayEntityId{42}};
    assert(entity.id() == GameplayEntityId{42});
    assert(entity.alive());
    assert(entity.component_count() == 0);

    // Label
    entity.set_label(SmallString{"hero"});
    assert(entity.label() == SmallString{"hero"});

    // Tags on entity
    entity.tags().add(GameplayTag{"class.warrior"});
    assert(entity.tags().has(GameplayTag{"class.warrior"}));

    // Attach component
    auto *health = new HealthComponent{};
    health->currentHp = 80.0;
    assert(entity.attach_component(health));
    assert(entity.component_count() == 1);
    assert(entity.has_component(HealthComponent::staticTypeId()));

    // Retrieve component
    auto *retrieved = entity.component(HealthComponent::staticTypeId());
    assert(retrieved != nullptr);
    assert(static_cast<HealthComponent *>(retrieved)->currentHp == 80.0);

    // Duplicate attach fails
    auto *health2 = new HealthComponent{};
    assert(!entity.attach_component(health2));
    delete health2; // Ownership not taken

    // Attach a second component type
    auto *mana = new ManaComponent{};
    assert(entity.attach_component(mana));
    assert(entity.component_count() == 2);

    // Detach
    auto *detached = entity.detach_component(ManaComponent::staticTypeId());
    assert(detached != nullptr);
    assert(entity.component_count() == 1);
    delete detached;

    // Detach non-existent
    assert(entity.detach_component(ManaComponent::staticTypeId()) == nullptr);

    // Null attach
    assert(!entity.attach_component(nullptr));

    // Mark dead
    entity.mark_dead();
    assert(!entity.alive());

    // for_each_component
    int visitCount = 0;
    entity.for_each_component([&](GameplayComponentTypeId, GameplayComponent &) { ++visitCount; });
    assert(visitCount == 1);

    // Move semantics
    GameplayEntity moved{static_cast<GameplayEntity &&>(entity)};
    assert(moved.id() == GameplayEntityId{42});
    assert(moved.component_count() == 1);
    // Original should be gutted
    assert(entity.component_count() == 0);
    assert(!entity.alive());

    fprintf(stderr, "[PASS] GameplayEntity\n");
  }

  // ====== GameplayWorld tests ======
  {
    GameplayWorld world{};
    assert(world.entity_count() == 0);

    // Create entities
    auto id1 = world.create_entity();
    auto id2 = world.create_entity(SmallString{"goblin"});
    assert(id1.valid());
    assert(id2.valid());
    assert(id1 != id2);
    assert(world.entity_count() == 2);

    // Lookup
    auto *e1 = world.entity(id1);
    assert(e1 != nullptr);
    assert(e1->id() == id1);

    auto *e2 = world.entity(id2);
    assert(e2 != nullptr);
    assert(e2->label() == SmallString{"goblin"});

    // Lookup non-existent
    assert(world.entity(GameplayEntityId{9999}) == nullptr);
    assert(!world.exists(GameplayEntityId{9999}));
    assert(world.exists(id1));

    // Tag queries on world
    e1->tags().add(GameplayTag{"type.player"});
    e2->tags().add(GameplayTag{"type.enemy"});
    e2->tags().add(GameplayTag{"status.hostile"});

    auto players = world.entities_with_tag(GameplayTag{"type.player"});
    assert(players.size() == 1);
    assert(players[0] == id1);

    auto typeEntities = world.entities_with_tag_child_of(GameplayTag{"type"});
    assert(typeEntities.size() == 2);

    // Attach component via world entity
    e1->attach_component(new HealthComponent{});
    assert(e1->has_component(HealthComponent::staticTypeId()));

    // Destroy
    assert(world.destroy_entity(id1));
    assert(world.entity_count() == 1);
    assert(!world.exists(id1));
    assert(world.entity(id1) == nullptr);

    // Destroy non-existent
    assert(!world.destroy_entity(id1));

    // for_each_entity
    int entityCount = 0;
    world.for_each_entity(
        [&](GameplayEntityId, GameplayEntity &) { ++entityCount; });
    assert(entityCount == 1);

    fprintf(stderr, "[PASS] GameplayWorld\n");
  }

  // ====== GameplayEvent & Dispatcher tests ======
  {
    GameplayEventDispatcher dispatcher{};
    assert(dispatcher.subscriber_count() == 0);
    assert(dispatcher.history().empty());

    GameplayEventTypeId damageType{10};
    GameplayEventTypeId healType{20};

    // Subscribe to specific type
    int damageCount = 0;
    double totalDamage = 0.0;
    auto sub1 = dispatcher.subscribe(damageType,
                                     [&](const GameplayEvent &e) {
                                       ++damageCount;
                                       totalDamage += e.numericValue;
                                     },
                                     GameplayEventPriority::normal);
    assert(sub1.valid());
    assert(dispatcher.subscriber_count() == 1);

    // Subscribe to all types
    int allCount = 0;
    auto sub2 = dispatcher.subscribe_all(
        [&](const GameplayEvent &) { ++allCount; }, GameplayEventPriority::normal);
    assert(sub2.valid());
    assert(dispatcher.subscriber_count() == 2);

    // Dispatch damage event
    dispatcher.emit(damageType, SmallString{"damage"}, GameplayEntityId{1}, GameplayEntityId{2},
                    25.0);
    assert(damageCount == 1);
    assert(totalDamage == 25.0);
    assert(allCount == 1);

    // Dispatch heal event — damage handler should NOT fire
    dispatcher.emit(healType, SmallString{"heal"}, GameplayEntityId{3}, GameplayEntityId{2}, 10.0);
    assert(damageCount == 1); // unchanged
    assert(allCount == 2);    // all-handler sees it

    // History
    assert(dispatcher.history().size() == 2);
    assert(dispatcher.history()[0].typeName == SmallString{"damage"});
    assert(dispatcher.history()[1].typeName == SmallString{"heal"});

    // Unsubscribe
    assert(dispatcher.unsubscribe(sub1));
    assert(dispatcher.subscriber_count() == 1);
    dispatcher.emit(damageType, SmallString{"damage"}, GameplayEntityId{1}, GameplayEntityId{2},
                    50.0);
    assert(damageCount == 1); // unchanged — unsubscribed
    assert(allCount == 3);

    // Unsubscribe non-existent
    assert(!dispatcher.unsubscribe(sub1));

    // Priority ordering
    GameplayEventDispatcher prioDispatcher{};
    std::vector<int> order{};

    prioDispatcher.subscribe(damageType,
                             [&](const GameplayEvent &) { order.push_back(3); },
                             GameplayEventPriority::low);
    prioDispatcher.subscribe(damageType,
                             [&](const GameplayEvent &) { order.push_back(1); },
                             GameplayEventPriority::highest);
    prioDispatcher.subscribe(damageType,
                             [&](const GameplayEvent &) { order.push_back(2); },
                             GameplayEventPriority::normal);

    prioDispatcher.emit(damageType, SmallString{"damage"}, GameplayEntityId{1});
    assert(order.size() == 3);
    assert(order[0] == 1); // highest first
    assert(order[1] == 2); // normal second
    assert(order[2] == 3); // low last

    // History capacity
    dispatcher.set_history_capacity(2);
    assert(dispatcher.history_capacity() == 2);
    // Already had 3 events, capacity 2 should trim
    assert(dispatcher.history().size() == 2);
    dispatcher.emit(damageType, SmallString{"damage"}, GameplayEntityId{1}, {}, 1.0);
    dispatcher.emit(damageType, SmallString{"damage"}, GameplayEntityId{1}, {}, 2.0);
    assert(dispatcher.history().size() == 2);

    // Clear history
    dispatcher.clear_history();
    assert(dispatcher.history().empty());

    fprintf(stderr, "[PASS] GameplayEvent & Dispatcher\n");
  }

  fprintf(stderr, "[ALL PASS] gameplay_core\n");
  return 0;
}
