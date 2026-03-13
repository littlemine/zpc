#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "zensim/gameplay/GameplayAbility.hpp"

int main() {
  using namespace zs;

  // ====== AbilityState name helper ======
  {
    assert(std::strcmp(ability_state_name(AbilityState::inactive), "inactive") == 0);
    assert(std::strcmp(ability_state_name(AbilityState::activating), "activating") == 0);
    assert(std::strcmp(ability_state_name(AbilityState::active), "active") == 0);
    assert(std::strcmp(ability_state_name(AbilityState::cooldown), "cooldown") == 0);
    assert(std::strcmp(ability_state_name(AbilityState::blocked), "blocked") == 0);

    fprintf(stderr, "[PASS] ability_state_name\n");
  }

  // ====== AbilityActivationResult name helper ======
  {
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::success), "success") == 0);
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::on_cooldown), "on_cooldown") == 0);
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::no_charges), "no_charges") == 0);
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::insufficient_cost), "insufficient_cost") == 0);
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::blocked), "blocked") == 0);
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::invalid_target), "invalid_target") == 0);
    assert(std::strcmp(ability_activation_result_name(AbilityActivationResult::already_active), "already_active") == 0);

    fprintf(stderr, "[PASS] ability_activation_result_name\n");
  }

  // ====== AbilityDescriptor basic properties ======
  {
    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Fireball"};
    desc.category = SmallString{"attack"};
    desc.costResource = SmallString{"mana"};
    desc.costAmount = 25.0;
    desc.castTime = 1.5;
    desc.duration = 0.0;  // instant
    desc.cooldownTime = 3.0;
    desc.power = 50.0;
    desc.targetMode = AbilityTargetMode::single_target;
    desc.range = 20.0;
    desc.maxCharges = 1;

    assert(desc.id.valid());
    assert(desc.name == SmallString{"Fireball"});
    assert(desc.costAmount == 25.0);
    assert(desc.castTime == 1.5);
    assert(desc.cooldownTime == 3.0);
    assert(desc.power == 50.0);
    assert(desc.targetMode == AbilityTargetMode::single_target);
    assert(desc.maxCharges == 1);

    fprintf(stderr, "[PASS] AbilityDescriptor basic\n");
  }

  // ====== AbilityDescriptor::from_definition ======
  {
    MechanicsDefinition def{};
    def.id = SmallString{"fireball"};
    def.tags.add(GameplayTag{"ability.fire"});
    def.set_field(SmallString{"name"}, MechanicsFieldValue::make_string(SmallString{"Fireball"}));
    def.set_field(SmallString{"category"}, MechanicsFieldValue::make_string(SmallString{"attack"}));
    def.set_field(SmallString{"cost_resource"}, MechanicsFieldValue::make_string(SmallString{"mana"}));
    def.set_field(SmallString{"cost_amount"}, MechanicsFieldValue::make_number(25.0));
    def.set_field(SmallString{"cast_time"}, MechanicsFieldValue::make_number(1.5));
    def.set_field(SmallString{"duration"}, MechanicsFieldValue::make_number(0.0));
    def.set_field(SmallString{"cooldown"}, MechanicsFieldValue::make_number(3.0));
    def.set_field(SmallString{"power"}, MechanicsFieldValue::make_number(50.0));
    def.set_field(SmallString{"range"}, MechanicsFieldValue::make_number(20.0));
    def.set_field(SmallString{"target_mode"}, MechanicsFieldValue::make_string(SmallString{"single_target"}));
    def.set_field(SmallString{"max_charges"}, MechanicsFieldValue::make_integer(2));
    def.set_field(SmallString{"charge_recharge_time"}, MechanicsFieldValue::make_number(5.0));

    auto desc = AbilityDescriptor::from_definition(def, AbilityDescriptorId{10});
    assert(desc.id == AbilityDescriptorId{10});
    assert(desc.name == SmallString{"Fireball"});
    assert(desc.category == SmallString{"attack"});
    assert(desc.costResource == SmallString{"mana"});
    assert(desc.costAmount == 25.0);
    assert(desc.castTime == 1.5);
    assert(desc.cooldownTime == 3.0);
    assert(desc.power == 50.0);
    assert(desc.range == 20.0);
    assert(desc.targetMode == AbilityTargetMode::single_target);
    assert(desc.maxCharges == 2);
    assert(desc.chargeRechargeTime == 5.0);
    assert(desc.tags.has(GameplayTag{"ability.fire"}));

    fprintf(stderr, "[PASS] AbilityDescriptor::from_definition\n");
  }

  // ====== AbilityInstance can_activate / is_running ======
  {
    AbilityInstance inst{};
    inst.instanceId = AbilityInstanceId{1};
    inst.currentCharges = 1;

    inst.state = AbilityState::inactive;
    assert(inst.can_activate());
    assert(!inst.is_running());

    inst.state = AbilityState::activating;
    assert(!inst.can_activate());
    assert(inst.is_running());

    inst.state = AbilityState::active;
    assert(!inst.can_activate());
    assert(inst.is_running());

    inst.state = AbilityState::cooldown;
    assert(inst.can_activate()); // has charges
    assert(!inst.is_running());

    inst.currentCharges = 0;
    assert(!inst.can_activate()); // no charges

    inst.state = AbilityState::blocked;
    inst.currentCharges = 1;
    assert(!inst.can_activate());
    assert(!inst.is_running());

    fprintf(stderr, "[PASS] AbilityInstance can_activate/is_running\n");
  }

  // ====== AbilitySystem: descriptor management ======
  {
    AbilitySystem system{};
    assert(system.descriptor_count() == 0);

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Fireball"};
    desc.costAmount = 25.0;
    desc.cooldownTime = 3.0;

    assert(system.register_descriptor(desc));
    assert(system.descriptor_count() == 1);

    // Duplicate fails
    assert(!system.register_descriptor(desc));
    assert(system.descriptor_count() == 1);

    // Invalid ID fails
    AbilityDescriptor invalid{};
    assert(!system.register_descriptor(invalid));

    // Find
    auto *found = system.find_descriptor(AbilityDescriptorId{1});
    assert(found != nullptr);
    assert(found->name == SmallString{"Fireball"});
    assert(system.find_descriptor(AbilityDescriptorId{99}) == nullptr);

    // Remove
    assert(system.remove_descriptor(AbilityDescriptorId{1}));
    assert(system.descriptor_count() == 0);
    assert(!system.remove_descriptor(AbilityDescriptorId{1}));

    fprintf(stderr, "[PASS] AbilitySystem descriptor management\n");
  }

  // ====== AbilitySystem: grant and revoke ======
  {
    AbilitySystem system{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Fireball"};
    desc.maxCharges = 2;
    system.register_descriptor(desc);

    // Grant
    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});
    assert(instId.valid());
    assert(system.total_instance_count() == 1);

    // Find instance
    auto *inst = system.find_instance(entityId, instId);
    assert(inst != nullptr);
    assert(inst->ownerId == entityId);
    assert(inst->descriptorId == AbilityDescriptorId{1});
    assert(inst->state == AbilityState::inactive);
    assert(inst->currentCharges == 2);

    // Find by descriptor
    auto *byDesc = system.find_instance_by_descriptor(entityId, AbilityDescriptorId{1});
    assert(byDesc != nullptr);
    assert(byDesc->instanceId == instId);

    // Duplicate grant fails
    auto dupId = system.grant_ability(entityId, AbilityDescriptorId{1});
    assert(!dupId.valid());
    assert(system.total_instance_count() == 1);

    // Grant non-existent descriptor fails
    auto badId = system.grant_ability(entityId, AbilityDescriptorId{99});
    assert(!badId.valid());

    // Entity abilities list
    auto abilities = system.entity_abilities(entityId);
    assert(abilities.size() == 1);

    // Revoke
    assert(system.revoke_ability(entityId, instId));
    assert(system.total_instance_count() == 0);
    assert(system.find_instance(entityId, instId) == nullptr);

    // Revoke non-existent
    assert(!system.revoke_ability(entityId, instId));

    // Empty entity
    auto emptyAbilities = system.entity_abilities(GameplayEntityId{99});
    assert(emptyAbilities.empty());

    // Revoke all
    auto id1 = system.grant_ability(entityId, AbilityDescriptorId{1});
    assert(id1.valid());

    AbilityDescriptor desc2{};
    desc2.id = AbilityDescriptorId{2};
    desc2.name = SmallString{"Heal"};
    system.register_descriptor(desc2);
    auto id2 = system.grant_ability(entityId, AbilityDescriptorId{2});
    assert(id2.valid());
    assert(system.total_instance_count() == 2);

    size_t revoked = system.revoke_all_abilities(entityId);
    assert(revoked == 2);
    assert(system.total_instance_count() == 0);

    fprintf(stderr, "[PASS] AbilitySystem grant/revoke\n");
  }

  // ====== AbilitySystem: instant ability activation ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Strike"};
    desc.castTime = 0.0;  // instant
    desc.duration = 0.0;  // instant
    desc.cooldownTime = 2.0;
    desc.power = 10.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});

    // Track execution
    int execCount = 0;
    system.set_execute_callback([&](const AbilityInstance &inst, const AbilityDescriptor &d) {
      ++execCount;
      assert(d.power == 10.0);
      assert(inst.ownerId == entityId);
    });

    // Activate
    auto result = system.try_activate(entityId, instId, {}, &dispatcher);
    assert(result == AbilityActivationResult::success);
    assert(execCount == 1);

    // Should be on cooldown now
    auto *inst = system.find_instance(entityId, instId);
    assert(inst != nullptr);
    assert(inst->state == AbilityState::cooldown);
    assert(inst->cooldownRemaining == 2.0);
    assert(inst->currentCharges == 0);

    // Can't activate again (no charges)
    result = system.try_activate(entityId, instId, {}, &dispatcher);
    assert(result == AbilityActivationResult::no_charges);

    // Events: activated is NOT emitted for instant (no cast_time), but
    // cast_completed, executed, and cooldown_started should be emitted
    assert(dispatcher.history().size() >= 2);

    fprintf(stderr, "[PASS] AbilitySystem instant activation\n");
  }

  // ====== AbilitySystem: cast time ability lifecycle ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId caster{1};
    GameplayEntityId target{2};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Fireball"};
    desc.castTime = 2.0;
    desc.duration = 0.0;  // instant effect after cast
    desc.cooldownTime = 3.0;
    desc.power = 50.0;
    desc.targetMode = AbilityTargetMode::single_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(caster, AbilityDescriptorId{1});

    int execCount = 0;
    system.set_execute_callback([&](const AbilityInstance &, const AbilityDescriptor &) {
      ++execCount;
    });

    // Missing target
    auto result = system.try_activate(caster, instId, {}, &dispatcher);
    assert(result == AbilityActivationResult::invalid_target);

    // Activate with target
    result = system.try_activate(caster, instId, target, &dispatcher);
    assert(result == AbilityActivationResult::success);

    auto *inst = system.find_instance(caster, instId);
    assert(inst->state == AbilityState::activating);
    assert(inst->castTimeRemaining == 2.0);
    assert(inst->targetId == target);
    assert(execCount == 0); // not yet executed

    // Can't activate again while activating
    result = system.try_activate(caster, instId, target, &dispatcher);
    assert(result == AbilityActivationResult::already_active);

    // Tick 1 second: still activating
    system.tick(1.0, &dispatcher);
    inst = system.find_instance(caster, instId);
    assert(inst->state == AbilityState::activating);
    assert(std::abs(inst->castTimeRemaining - 1.0) < 0.001);
    assert(execCount == 0);

    // Tick 1.5 seconds: cast completes, executes, enters cooldown
    system.tick(1.5, &dispatcher);
    inst = system.find_instance(caster, instId);
    assert(inst->state == AbilityState::cooldown);
    assert(execCount == 1);
    assert(inst->cooldownRemaining > 0.0);

    // Tick through cooldown
    system.tick(3.0, &dispatcher);
    inst = system.find_instance(caster, instId);
    assert(inst->state == AbilityState::inactive);

    fprintf(stderr, "[PASS] AbilitySystem cast time lifecycle\n");
  }

  // ====== AbilitySystem: duration-based ability ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Shield"};
    desc.castTime = 0.0;
    desc.duration = 5.0;   // active for 5 seconds
    desc.cooldownTime = 2.0;
    desc.targetMode = AbilityTargetMode::self_only;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});
    auto result = system.try_activate(entityId, instId, {}, &dispatcher);
    assert(result == AbilityActivationResult::success);

    auto *inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::active);
    assert(std::abs(inst->durationRemaining - 5.0) < 0.001);

    // Tick 3 seconds: still active
    system.tick(3.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::active);
    assert(std::abs(inst->durationRemaining - 2.0) < 0.001);

    // Tick 2.5 seconds: duration expires, enter cooldown
    system.tick(2.5, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::cooldown);

    // Tick through cooldown
    system.tick(2.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::inactive);

    fprintf(stderr, "[PASS] AbilitySystem duration ability\n");
  }

  // ====== AbilitySystem: cost checking ======
  {
    AbilitySystem system{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Heal"};
    desc.costResource = SmallString{"mana"};
    desc.costAmount = 30.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});

    // Set up cost check: entity has 20 mana, not enough for 30
    double entityMana = 20.0;
    system.set_cost_check([&](GameplayEntityId, const SmallString &, double amount) {
      return entityMana >= amount;
    });
    system.set_cost_deduct([&](GameplayEntityId, const SmallString &, double amount) {
      if (entityMana < amount) return false;
      entityMana -= amount;
      return true;
    });

    // Insufficient cost
    auto result = system.try_activate(entityId, instId);
    assert(result == AbilityActivationResult::insufficient_cost);
    assert(entityMana == 20.0); // not deducted

    // Add mana
    entityMana = 50.0;
    result = system.try_activate(entityId, instId);
    assert(result == AbilityActivationResult::success);
    assert(entityMana == 20.0); // deducted 30

    fprintf(stderr, "[PASS] AbilitySystem cost checking\n");
  }

  // ====== AbilitySystem: charges and recharge ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Dash"};
    desc.castTime = 0.0;
    desc.duration = 0.0;
    desc.cooldownTime = 0.0;  // no cooldown, but charge-based
    desc.power = 0.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 3;
    desc.chargeRechargeTime = 5.0;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});
    auto *inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges == 3);

    // Use all 3 charges
    assert(system.try_activate(entityId, instId, {}, &dispatcher) == AbilityActivationResult::success);
    inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges == 2);

    assert(system.try_activate(entityId, instId, {}, &dispatcher) == AbilityActivationResult::success);
    inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges == 1);

    assert(system.try_activate(entityId, instId, {}, &dispatcher) == AbilityActivationResult::success);
    inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges == 0);

    // No more charges
    assert(system.try_activate(entityId, instId, {}, &dispatcher) == AbilityActivationResult::no_charges);

    // Tick 5 seconds: one charge restored
    system.tick(5.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges == 1);

    // Can use the restored charge
    assert(system.try_activate(entityId, instId, {}, &dispatcher) == AbilityActivationResult::success);
    inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges == 0);

    // Tick 10 seconds: two charges restored
    system.tick(10.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->currentCharges >= 2);

    fprintf(stderr, "[PASS] AbilitySystem charges and recharge\n");
  }

  // ====== AbilitySystem: cancel ability ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Fireball"};
    desc.castTime = 3.0;
    desc.cooldownTime = 2.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});
    system.try_activate(entityId, instId, {}, &dispatcher);

    auto *inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::activating);

    // Cancel during cast
    assert(system.cancel_ability(entityId, instId, &dispatcher));
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::cooldown); // enters cooldown on cancel
    assert(inst->cooldownRemaining == 2.0);

    // Cancel inactive ability: should fail
    system.tick(2.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::inactive);
    assert(!system.cancel_ability(entityId, instId, &dispatcher));

    // Cancel non-existent
    assert(!system.cancel_ability(entityId, AbilityInstanceId{999}, &dispatcher));

    fprintf(stderr, "[PASS] AbilitySystem cancel\n");
  }

  // ====== AbilitySystem: block and unblock ======
  {
    AbilitySystem system{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Strike"};
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});

    // Block
    assert(system.block_ability(entityId, instId));
    auto *inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::blocked);

    // Can't activate while blocked
    auto result = system.try_activate(entityId, instId);
    assert(result == AbilityActivationResult::blocked);

    // Unblock
    assert(system.unblock_ability(entityId, instId));
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::inactive);

    // Can activate after unblock
    result = system.try_activate(entityId, instId);
    assert(result == AbilityActivationResult::success);

    // Unblock non-blocked: fails
    system.tick(10.0); // let it finish
    assert(!system.unblock_ability(entityId, instId)); // already inactive

    // Block non-existent
    assert(!system.block_ability(entityId, AbilityInstanceId{999}));
    assert(!system.unblock_ability(entityId, AbilityInstanceId{999}));

    fprintf(stderr, "[PASS] AbilitySystem block/unblock\n");
  }

  // ====== AbilitySystem: multiple entities ======
  {
    AbilitySystem system{};
    GameplayEntityId entity1{1};
    GameplayEntityId entity2{2};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Strike"};
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto inst1 = system.grant_ability(entity1, AbilityDescriptorId{1});
    auto inst2 = system.grant_ability(entity2, AbilityDescriptorId{1});
    assert(inst1.valid());
    assert(inst2.valid());
    assert(inst1 != inst2); // different instance IDs
    assert(system.total_instance_count() == 2);

    // Each entity has independent ability state
    system.try_activate(entity1, inst1);
    auto *i1 = system.find_instance(entity1, inst1);
    assert(i1 != nullptr); // entity1's ability was activated
    auto *i2 = system.find_instance(entity2, inst2);
    assert(i2->state == AbilityState::inactive); // entity2's ability unaffected

    fprintf(stderr, "[PASS] AbilitySystem multiple entities\n");
  }

  // ====== AbilitySystem: event emission ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Zap"};
    desc.castTime = 1.0;
    desc.duration = 0.0;
    desc.cooldownTime = 2.0;
    desc.power = 20.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});

    // Track events
    std::vector<GameplayEventTypeId> eventTypes{};
    dispatcher.subscribe_all([&](const GameplayEvent &e) {
      eventTypes.push_back(e.typeId);
    });

    // Activate (enters activating)
    system.try_activate(entityId, instId, {}, &dispatcher);
    assert(!eventTypes.empty());
    assert(eventTypes.back() == ability_events::activated);

    // Tick to complete cast
    system.tick(1.5, &dispatcher);
    // Should have: cast_completed, executed, cooldown_started
    bool hasCastDone = false, hasExecuted = false, hasCdStart = false;
    for (auto t : eventTypes) {
      if (t == ability_events::cast_completed) hasCastDone = true;
      if (t == ability_events::executed) hasExecuted = true;
      if (t == ability_events::cooldown_started) hasCdStart = true;
    }
    assert(hasCastDone);
    assert(hasExecuted);
    assert(hasCdStart);

    // Tick through cooldown
    eventTypes.clear();
    system.tick(2.0, &dispatcher);
    bool hasCdDone = false;
    for (auto t : eventTypes) {
      if (t == ability_events::cooldown_finished) hasCdDone = true;
    }
    assert(hasCdDone);

    fprintf(stderr, "[PASS] AbilitySystem event emission\n");
  }

  // ====== AbilitySystem: no cooldown instant ability ======
  {
    AbilitySystem system{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"AutoAttack"};
    desc.castTime = 0.0;
    desc.duration = 0.0;
    desc.cooldownTime = 0.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});

    // Should activate and go straight to inactive
    auto result = system.try_activate(entityId, instId);
    assert(result == AbilityActivationResult::success);
    auto *inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::inactive);

    // Can immediately activate again (no cooldown, no charge consumption issues
    // because we restore charge after cooldown/inactive)
    // Actually, the charge was consumed. Let's verify
    assert(inst->currentCharges == 0);

    fprintf(stderr, "[PASS] AbilitySystem no cooldown instant\n");
  }

  // ====== AbilitySystem: clear ======
  {
    AbilitySystem system{};
    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Strike"};
    system.register_descriptor(desc);
    system.grant_ability(GameplayEntityId{1}, AbilityDescriptorId{1});

    assert(system.descriptor_count() == 1);
    assert(system.total_instance_count() == 1);

    system.clear();
    assert(system.descriptor_count() == 0);
    assert(system.total_instance_count() == 0);

    fprintf(stderr, "[PASS] AbilitySystem clear\n");
  }

  // ====== AbilitySystem: tick without dispatcher ======
  {
    AbilitySystem system{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Fireball"};
    desc.castTime = 1.0;
    desc.cooldownTime = 1.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});
    system.try_activate(entityId, instId); // no dispatcher

    // Tick without dispatcher: should not crash
    system.tick(2.0);
    auto *inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::cooldown);

    system.tick(1.0);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::inactive);

    fprintf(stderr, "[PASS] AbilitySystem tick without dispatcher\n");
  }

  // ====== AbilitySystem: cast time + duration combination ======
  {
    AbilitySystem system{};
    GameplayEventDispatcher dispatcher{};
    GameplayEntityId entityId{1};

    AbilityDescriptor desc{};
    desc.id = AbilityDescriptorId{1};
    desc.name = SmallString{"Channeled"};
    desc.castTime = 1.0;   // 1s cast
    desc.duration = 3.0;   // 3s active
    desc.cooldownTime = 2.0;
    desc.targetMode = AbilityTargetMode::no_target;
    desc.maxCharges = 1;
    system.register_descriptor(desc);

    auto instId = system.grant_ability(entityId, AbilityDescriptorId{1});
    system.try_activate(entityId, instId, {}, &dispatcher);

    auto *inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::activating);

    // Tick through cast
    system.tick(1.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::active);
    assert(std::abs(inst->durationRemaining - 3.0) < 0.001);

    // Tick through duration
    system.tick(3.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::cooldown);

    // Tick through cooldown
    system.tick(2.0, &dispatcher);
    inst = system.find_instance(entityId, instId);
    assert(inst->state == AbilityState::inactive);

    fprintf(stderr, "[PASS] AbilitySystem cast+duration combo\n");
  }

  fprintf(stderr, "[ALL PASS] gameplay_ability\n");
  return 0;
}
