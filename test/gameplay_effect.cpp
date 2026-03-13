#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "zensim/gameplay/GameplayEffect.hpp"

int main() {
  using namespace zs;

  // ====== EffectDurationType name helper ======
  {
    assert(std::strcmp(effect_duration_type_name(EffectDurationType::instant), "instant") == 0);
    assert(std::strcmp(effect_duration_type_name(EffectDurationType::duration), "duration") == 0);
    assert(std::strcmp(effect_duration_type_name(EffectDurationType::periodic), "periodic") == 0);
    assert(std::strcmp(effect_duration_type_name(EffectDurationType::infinite), "infinite") == 0);

    fprintf(stderr, "[PASS] effect_duration_type_name\n");
  }

  // ====== StatModOp name helper ======
  {
    assert(std::strcmp(stat_mod_op_name(StatModOp::additive), "additive") == 0);
    assert(std::strcmp(stat_mod_op_name(StatModOp::multiplicative), "multiplicative") == 0);
    assert(std::strcmp(stat_mod_op_name(StatModOp::base_override), "base_override") == 0);
    assert(std::strcmp(stat_mod_op_name(StatModOp::final_override), "final_override") == 0);

    fprintf(stderr, "[PASS] stat_mod_op_name\n");
  }

  // ====== EffectDescriptorId / EffectInstanceId basics ======
  {
    EffectDescriptorId d0{};
    assert(!d0.valid());
    EffectDescriptorId d1{42};
    assert(d1.valid());
    assert(d0 != d1);
    assert(d1 == EffectDescriptorId{42});

    EffectInstanceId i0{};
    assert(!i0.valid());
    EffectInstanceId i1{7};
    assert(i1.valid());
    assert(i0 != i1);
    assert(i1 == EffectInstanceId{7});

    fprintf(stderr, "[PASS] EffectDescriptorId / EffectInstanceId\n");
  }

  // ====== StatBlock: set_base, base, compute without modifiers ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"strength"}, 100.0);
    assert(sb.base(SmallString{"strength"}) == 100.0);
    assert(sb.base(SmallString{"agility"}) == 0.0);  // default
    assert(sb.compute(SmallString{"strength"}) == 100.0);  // no modifiers = base

    fprintf(stderr, "[PASS] StatBlock base values\n");
  }

  // ====== StatBlock: additive modifier ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"hp"}, 100.0);
    sb.add_modifier(StatModifier{SmallString{"hp"}, StatModOp::additive, 25.0, EffectInstanceId{1}, 0});
    sb.add_modifier(StatModifier{SmallString{"hp"}, StatModOp::additive, 10.0, EffectInstanceId{2}, 0});
    assert(sb.compute(SmallString{"hp"}) == 135.0);
    assert(sb.modifier_count() == 2);

    fprintf(stderr, "[PASS] StatBlock additive modifiers\n");
  }

  // ====== StatBlock: multiplicative modifier ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"damage"}, 50.0);
    sb.add_modifier(StatModifier{SmallString{"damage"}, StatModOp::additive, 10.0, EffectInstanceId{1}, 0});
    sb.add_modifier(StatModifier{SmallString{"damage"}, StatModOp::multiplicative, 1.5, EffectInstanceId{2}, 0});
    // (50 + 10) * 1.5 = 90
    assert(std::abs(sb.compute(SmallString{"damage"}) - 90.0) < 1e-9);

    fprintf(stderr, "[PASS] StatBlock multiplicative modifier\n");
  }

  // ====== StatBlock: base_override ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"armor"}, 30.0);
    sb.add_modifier(StatModifier{SmallString{"armor"}, StatModOp::base_override, 50.0, EffectInstanceId{1}, 0});
    sb.add_modifier(StatModifier{SmallString{"armor"}, StatModOp::additive, 10.0, EffectInstanceId{2}, 0});
    // base_override replaces base: 50 + 10 = 60
    assert(std::abs(sb.compute(SmallString{"armor"}) - 60.0) < 1e-9);

    fprintf(stderr, "[PASS] StatBlock base_override\n");
  }

  // ====== StatBlock: final_override ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"speed"}, 100.0);
    sb.add_modifier(StatModifier{SmallString{"speed"}, StatModOp::additive, 50.0, EffectInstanceId{1}, 0});
    sb.add_modifier(StatModifier{SmallString{"speed"}, StatModOp::final_override, 999.0, EffectInstanceId{2}, 0});
    // final_override replaces everything
    assert(sb.compute(SmallString{"speed"}) == 999.0);

    fprintf(stderr, "[PASS] StatBlock final_override\n");
  }

  // ====== StatBlock: priority ordering ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"x"}, 10.0);
    // Two base_overrides with different priorities -- last (higher priority) wins
    sb.add_modifier(StatModifier{SmallString{"x"}, StatModOp::base_override, 20.0, EffectInstanceId{1}, 1});
    sb.add_modifier(StatModifier{SmallString{"x"}, StatModOp::base_override, 30.0, EffectInstanceId{2}, 2});
    assert(sb.compute(SmallString{"x"}) == 30.0);  // higher priority = 30

    fprintf(stderr, "[PASS] StatBlock priority ordering\n");
  }

  // ====== StatBlock: remove_modifiers_from ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"mp"}, 200.0);
    sb.add_modifier(StatModifier{SmallString{"mp"}, StatModOp::additive, 50.0, EffectInstanceId{10}, 0});
    sb.add_modifier(StatModifier{SmallString{"mp"}, StatModOp::additive, 30.0, EffectInstanceId{20}, 0});
    sb.add_modifier(StatModifier{SmallString{"mp"}, StatModOp::additive, 20.0, EffectInstanceId{10}, 0});

    assert(sb.modifier_count() == 3);
    size_t removed = sb.remove_modifiers_from(EffectInstanceId{10});
    assert(removed == 2);
    assert(sb.modifier_count() == 1);
    assert(sb.compute(SmallString{"mp"}) == 230.0);  // 200 + 30

    fprintf(stderr, "[PASS] StatBlock remove_modifiers_from\n");
  }

  // ====== StatBlock: stat_names and clear ======
  {
    StatBlock sb{};
    sb.set_base(SmallString{"a"}, 1.0);
    sb.set_base(SmallString{"b"}, 2.0);
    auto names = sb.stat_names();
    assert(names.size() == 2);
    sb.clear();
    assert(sb.stat_names().empty());
    assert(sb.modifier_count() == 0);

    fprintf(stderr, "[PASS] StatBlock stat_names and clear\n");
  }

  // ====== EffectSystem: register/find/remove descriptor ======
  {
    EffectSystem sys{};
    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{1};
    desc.name = SmallString{"fire_dot"};
    desc.durationType = EffectDurationType::periodic;
    desc.duration = 5.0;
    desc.period = 1.0;
    desc.magnitude = 10.0;

    assert(sys.register_descriptor(desc));
    assert(sys.descriptor_count() == 1);
    assert(!sys.register_descriptor(desc));  // duplicate
    assert(sys.descriptor_count() == 1);

    auto *found = sys.find_descriptor(EffectDescriptorId{1});
    assert(found != nullptr);
    assert(found->name == SmallString{"fire_dot"});

    assert(sys.find_descriptor(EffectDescriptorId{99}) == nullptr);

    assert(sys.remove_descriptor(EffectDescriptorId{1}));
    assert(sys.descriptor_count() == 0);
    assert(!sys.remove_descriptor(EffectDescriptorId{1}));

    fprintf(stderr, "[PASS] EffectSystem descriptor management\n");
  }

  // ====== EffectSystem: apply instant effect ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{1};
    desc.name = SmallString{"heal"};
    desc.durationType = EffectDurationType::instant;
    desc.magnitude = 50.0;
    sys.register_descriptor(desc);

    bool callbackFired = false;
    double callbackMag = 0.0;
    sys.set_instant_apply_callback([&](const EffectInstance &inst,
                                        const EffectDescriptor &d) {
      callbackFired = true;
      callbackMag = d.magnitude;
    });

    GameplayEntityId target{100};
    GameplayEntityId source{200};
    auto instId = sys.apply_effect(EffectDescriptorId{1}, target, source, &dispatcher);
    assert(instId.valid());
    assert(callbackFired);
    assert(callbackMag == 50.0);

    // Instant effects don't persist
    assert(sys.effect_count(target) == 0);
    assert(!sys.has_effect(target, EffectDescriptorId{1}));

    // Check event was emitted
    assert(dispatcher.history().size() == 1);

    fprintf(stderr, "[PASS] EffectSystem apply instant effect\n");
  }

  // ====== EffectSystem: apply duration effect ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{2};
    desc.name = SmallString{"shield"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 10.0;
    desc.magnitude = 100.0;

    // Add a stat modifier to the descriptor
    StatModifier mod{};
    mod.statName = SmallString{"armor"};
    mod.operation = StatModOp::additive;
    mod.value = 50.0;
    desc.modifiers.push_back(mod);

    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    // Set up base stat
    sys.stat_block(target).set_base(SmallString{"armor"}, 100.0);

    auto instId = sys.apply_effect(EffectDescriptorId{2}, target, {}, &dispatcher);
    assert(instId.valid());
    assert(sys.effect_count(target) == 1);
    assert(sys.has_effect(target, EffectDescriptorId{2}));

    // Stat should reflect modifier
    assert(sys.stat_block(target).compute(SmallString{"armor"}) == 150.0);

    // Find the effect instance
    auto *inst = sys.find_effect(target, instId);
    assert(inst != nullptr);
    assert(inst->active);
    assert(inst->durationRemaining == 10.0);

    fprintf(stderr, "[PASS] EffectSystem apply duration effect\n");
  }

  // ====== EffectSystem: apply infinite effect ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{3};
    desc.name = SmallString{"passive_aura"};
    desc.durationType = EffectDurationType::infinite;
    desc.magnitude = 5.0;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto instId = sys.apply_effect(EffectDescriptorId{3}, target);
    assert(instId.valid());
    assert(sys.effect_count(target) == 1);

    // Tick a large amount -- infinite effects don't expire
    sys.tick(9999.0);
    assert(sys.effect_count(target) == 1);

    fprintf(stderr, "[PASS] EffectSystem apply infinite effect\n");
  }

  // ====== EffectSystem: duration expiry via tick ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{4};
    desc.name = SmallString{"buff"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 3.0;

    StatModifier mod{};
    mod.statName = SmallString{"str"};
    mod.operation = StatModOp::additive;
    mod.value = 10.0;
    desc.modifiers.push_back(mod);

    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    sys.stat_block(target).set_base(SmallString{"str"}, 50.0);

    auto instId = sys.apply_effect(EffectDescriptorId{4}, target, {}, &dispatcher);
    assert(instId.valid());
    assert(sys.stat_block(target).compute(SmallString{"str"}) == 60.0);

    // Tick 2s -- still active
    sys.tick(2.0, &dispatcher);
    assert(sys.effect_count(target) == 1);
    auto *inst = sys.find_effect(target, instId);
    assert(inst != nullptr);
    assert(inst->durationRemaining == 1.0);

    // Tick 1.5s -- expires
    sys.tick(1.5, &dispatcher);
    assert(sys.effect_count(target) == 0);

    // Modifier should be removed
    assert(sys.stat_block(target).compute(SmallString{"str"}) == 50.0);

    // Should have: applied, expired events
    bool hasExpired = false;
    for (size_t i = 0; i < dispatcher.history().size(); ++i) {
      const auto &e = dispatcher.history()[i];
      if (e.typeId == effect_events::expired) hasExpired = true;
    }
    assert(hasExpired);

    fprintf(stderr, "[PASS] EffectSystem duration expiry\n");
  }

  // ====== EffectSystem: periodic effect with tick callback ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{5};
    desc.name = SmallString{"poison"};
    desc.durationType = EffectDurationType::periodic;
    desc.duration = 4.0;
    desc.period = 1.0;
    desc.magnitude = 5.0;
    sys.register_descriptor(desc);

    int tickCount = 0;
    sys.set_periodic_tick_callback([&](const EffectInstance &inst,
                                       const EffectDescriptor &d) {
      ++tickCount;
    });

    GameplayEntityId target{1};
    auto instId = sys.apply_effect(EffectDescriptorId{5}, target, {}, &dispatcher);
    assert(instId.valid());

    // Tick 2.5s: should fire 2 periodic ticks (at 1.0 and 2.0)
    // Initial periodTimer = 1.0
    // After subtracting 2.5: periodTimer = -1.5
    // Loop: periodTimer += 1.0 -> -0.5 (tick 1), += 1.0 -> 0.5 (tick 2)
    sys.tick(2.5, &dispatcher);
    assert(tickCount == 2);

    auto *inst = sys.find_effect(target, instId);
    assert(inst != nullptr);
    assert(inst->tickCount == 2);
    assert(inst->durationRemaining == 1.5);

    // Tick 2.0s more -- should fire 1 more tick before expiry
    // periodTimer = 0.5, subtract 2.0 -> -1.5
    // Loop: += 1.0 -> -0.5 (tick 3), += 1.0 -> 0.5 (tick 4 -- but effect expires)
    // Actually, duration would go to -0.5, so it expires. The periodic ticks
    // happen before the duration check.
    sys.tick(2.0, &dispatcher);
    // Effect should be expired now
    assert(sys.effect_count(target) == 0);
    // Total tick count: at least 3 more possible
    assert(tickCount >= 3);

    fprintf(stderr, "[PASS] EffectSystem periodic tick\n");
  }

  // ====== EffectSystem: stacking - replace ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{10};
    desc.name = SmallString{"regen"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 5.0;
    desc.stackPolicy = EffectStackPolicy::replace;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto id1 = sys.apply_effect(EffectDescriptorId{10}, target, {}, &dispatcher);
    assert(id1.valid());

    // Tick 2s
    sys.tick(2.0);
    auto *inst = sys.find_effect(target, id1);
    assert(inst != nullptr);
    assert(std::abs(inst->durationRemaining - 3.0) < 1e-9);

    // Re-apply (replace) -- should reset timer, same instance
    auto id2 = sys.apply_effect(EffectDescriptorId{10}, target, {}, &dispatcher);
    assert(id2 == id1);  // same instance
    assert(sys.effect_count(target) == 1);

    inst = sys.find_effect(target, id1);
    assert(inst != nullptr);
    assert(std::abs(inst->durationRemaining - 5.0) < 1e-9);  // timer reset

    fprintf(stderr, "[PASS] EffectSystem stacking replace\n");
  }

  // ====== EffectSystem: stacking - stack_count ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{11};
    desc.name = SmallString{"bleed"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 8.0;
    desc.stackPolicy = EffectStackPolicy::stack_count;
    desc.maxStacks = 3;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto id1 = sys.apply_effect(EffectDescriptorId{11}, target, {}, &dispatcher);
    assert(id1.valid());
    auto *inst = sys.find_effect(target, id1);
    assert(inst != nullptr);
    assert(inst->stackCount == 1);

    // Apply again -- stack count goes up
    auto id2 = sys.apply_effect(EffectDescriptorId{11}, target, {}, &dispatcher);
    assert(id2 == id1);
    inst = sys.find_effect(target, id1);
    assert(inst->stackCount == 2);

    // Apply again -- stack count to max (3)
    sys.apply_effect(EffectDescriptorId{11}, target, {}, &dispatcher);
    inst = sys.find_effect(target, id1);
    assert(inst->stackCount == 3);

    // Apply again at max -- should not exceed
    sys.apply_effect(EffectDescriptorId{11}, target, {}, &dispatcher);
    inst = sys.find_effect(target, id1);
    assert(inst->stackCount == 3);

    // Still only 1 effect instance
    assert(sys.effect_count(target) == 1);

    // Check that stacked event was emitted
    bool hasStacked = false;
    for (size_t i = 0; i < dispatcher.history().size(); ++i) {
      const auto &e = dispatcher.history()[i];
      if (e.typeId == effect_events::stacked) hasStacked = true;
    }
    assert(hasStacked);

    fprintf(stderr, "[PASS] EffectSystem stacking stack_count\n");
  }

  // ====== EffectSystem: stacking - stack_duration ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{12};
    desc.name = SmallString{"slow"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 3.0;
    desc.stackPolicy = EffectStackPolicy::stack_duration;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto id1 = sys.apply_effect(EffectDescriptorId{12}, target);
    assert(id1.valid());
    auto *inst = sys.find_effect(target, id1);
    assert(inst != nullptr);
    assert(std::abs(inst->durationRemaining - 3.0) < 1e-9);

    // Apply again -- extends duration
    auto id2 = sys.apply_effect(EffectDescriptorId{12}, target);
    assert(id2 == id1);
    inst = sys.find_effect(target, id1);
    assert(std::abs(inst->durationRemaining - 6.0) < 1e-9);

    assert(sys.effect_count(target) == 1);

    fprintf(stderr, "[PASS] EffectSystem stacking stack_duration\n");
  }

  // ====== EffectSystem: stacking - independent ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{13};
    desc.name = SmallString{"dot_tick"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 5.0;
    desc.stackPolicy = EffectStackPolicy::independent;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto id1 = sys.apply_effect(EffectDescriptorId{13}, target);
    assert(id1.valid());

    auto id2 = sys.apply_effect(EffectDescriptorId{13}, target);
    assert(id2.valid());
    assert(id2 != id1);  // different instances

    auto id3 = sys.apply_effect(EffectDescriptorId{13}, target);
    assert(id3.valid());
    assert(id3 != id1 && id3 != id2);

    // 3 independent instances
    assert(sys.effect_count(target) == 3);

    fprintf(stderr, "[PASS] EffectSystem stacking independent\n");
  }

  // ====== EffectSystem: immunity via granted tags ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    // First: an effect that grants an immunity tag
    EffectDescriptor shield{};
    shield.id = EffectDescriptorId{20};
    shield.name = SmallString{"fire_shield"};
    shield.durationType = EffectDurationType::infinite;
    shield.grantedTags.add(GameplayTag{SmallString{"immunity.fire"}});
    sys.register_descriptor(shield);

    // Second: a fire effect that is blocked by fire immunity
    EffectDescriptor fireball{};
    fireball.id = EffectDescriptorId{21};
    fireball.name = SmallString{"fireball_dot"};
    fireball.durationType = EffectDurationType::duration;
    fireball.duration = 5.0;
    fireball.magnitude = 20.0;
    fireball.immunityTags.add(GameplayTag{SmallString{"immunity.fire"}});
    sys.register_descriptor(fireball);

    GameplayEntityId target{1};

    // Apply fire without shield -- should work
    auto id1 = sys.apply_effect(EffectDescriptorId{21}, target, {}, &dispatcher);
    assert(id1.valid());
    assert(sys.effect_count(target) == 1);

    // Clean up
    sys.remove_all_effects(target);

    // Now apply shield first
    auto shieldId = sys.apply_effect(EffectDescriptorId{20}, target);
    assert(shieldId.valid());
    assert(sys.has_granted_tag(target, GameplayTag{SmallString{"immunity.fire"}}));

    // Try to apply fire -- should be immune
    auto id2 = sys.apply_effect(EffectDescriptorId{21}, target, {}, &dispatcher);
    assert(!id2.valid());  // rejected
    assert(sys.effect_count(target) == 1);  // only shield

    // Check immune event
    bool hasImmune = false;
    for (size_t i = 0; i < dispatcher.history().size(); ++i) {
      const auto &e = dispatcher.history()[i];
      if (e.typeId == effect_events::immune) hasImmune = true;
    }
    assert(hasImmune);

    fprintf(stderr, "[PASS] EffectSystem immunity\n");
  }

  // ====== EffectSystem: cancellation via tags ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    // A burning effect
    EffectDescriptor burning{};
    burning.id = EffectDescriptorId{30};
    burning.name = SmallString{"burning"};
    burning.durationType = EffectDurationType::duration;
    burning.duration = 10.0;
    burning.tags.add(GameplayTag{SmallString{"debuff.fire"}});
    sys.register_descriptor(burning);

    // A cleanse effect that cancels fire debuffs
    EffectDescriptor cleanse{};
    cleanse.id = EffectDescriptorId{31};
    cleanse.name = SmallString{"cleanse"};
    cleanse.durationType = EffectDurationType::instant;
    cleanse.cancellationTags.add(GameplayTag{SmallString{"debuff.fire"}});
    sys.register_descriptor(cleanse);

    GameplayEntityId target{1};

    // Apply burning
    auto burnId = sys.apply_effect(EffectDescriptorId{30}, target, {}, &dispatcher);
    assert(burnId.valid());
    assert(sys.effect_count(target) == 1);

    // Apply cleanse -- should remove burning
    auto cleanseId = sys.apply_effect(EffectDescriptorId{31}, target, {}, &dispatcher);
    assert(cleanseId.valid());

    // Burning should be cancelled
    assert(sys.effect_count(target) == 0);

    // Check cancelled event
    bool hasCancelled = false;
    for (size_t i = 0; i < dispatcher.history().size(); ++i) {
      const auto &e = dispatcher.history()[i];
      if (e.typeId == effect_events::cancelled) hasCancelled = true;
    }
    assert(hasCancelled);

    fprintf(stderr, "[PASS] EffectSystem cancellation\n");
  }

  // ====== EffectSystem: remove_effect ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{40};
    desc.name = SmallString{"buff_a"};
    desc.durationType = EffectDurationType::infinite;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto instId = sys.apply_effect(EffectDescriptorId{40}, target, {}, &dispatcher);
    assert(instId.valid());
    assert(sys.effect_count(target) == 1);

    assert(sys.remove_effect(target, instId, &dispatcher));
    assert(sys.effect_count(target) == 0);
    assert(!sys.remove_effect(target, instId));  // already removed

    // Check removed event
    bool hasRemoved = false;
    for (size_t i = 0; i < dispatcher.history().size(); ++i) {
      const auto &e = dispatcher.history()[i];
      if (e.typeId == effect_events::removed) hasRemoved = true;
    }
    assert(hasRemoved);

    fprintf(stderr, "[PASS] EffectSystem remove_effect\n");
  }

  // ====== EffectSystem: remove_all_effects ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor d1{};
    d1.id = EffectDescriptorId{50};
    d1.name = SmallString{"e1"};
    d1.durationType = EffectDurationType::infinite;
    sys.register_descriptor(d1);

    EffectDescriptor d2{};
    d2.id = EffectDescriptorId{51};
    d2.name = SmallString{"e2"};
    d2.durationType = EffectDurationType::infinite;
    sys.register_descriptor(d2);

    GameplayEntityId target{1};
    sys.apply_effect(EffectDescriptorId{50}, target);
    sys.apply_effect(EffectDescriptorId{51}, target);
    assert(sys.effect_count(target) == 2);

    size_t removed = sys.remove_all_effects(target, &dispatcher);
    assert(removed == 2);
    assert(sys.effect_count(target) == 0);
    assert(sys.remove_all_effects(target) == 0);  // already empty

    fprintf(stderr, "[PASS] EffectSystem remove_all_effects\n");
  }

  // ====== EffectSystem: entity_effects query ======
  {
    EffectSystem sys{};

    EffectDescriptor d1{};
    d1.id = EffectDescriptorId{60};
    d1.name = SmallString{"e1"};
    d1.durationType = EffectDurationType::infinite;
    sys.register_descriptor(d1);

    EffectDescriptor d2{};
    d2.id = EffectDescriptorId{61};
    d2.name = SmallString{"e2"};
    d2.durationType = EffectDurationType::infinite;
    sys.register_descriptor(d2);

    GameplayEntityId target{1};
    auto id1 = sys.apply_effect(EffectDescriptorId{60}, target);
    auto id2 = sys.apply_effect(EffectDescriptorId{61}, target);

    auto effects = sys.entity_effects(target);
    assert(effects.size() == 2);

    // Check for a non-existent entity
    auto empty = sys.entity_effects(GameplayEntityId{999});
    assert(empty.empty());

    fprintf(stderr, "[PASS] EffectSystem entity_effects\n");
  }

  // ====== EffectSystem: total_effect_count ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{70};
    desc.name = SmallString{"eff"};
    desc.durationType = EffectDurationType::infinite;
    sys.register_descriptor(desc);

    sys.apply_effect(EffectDescriptorId{70}, GameplayEntityId{1});
    sys.apply_effect(EffectDescriptorId{70}, GameplayEntityId{2});
    sys.apply_effect(EffectDescriptorId{70}, GameplayEntityId{3});

    assert(sys.total_effect_count() == 3);

    fprintf(stderr, "[PASS] EffectSystem total_effect_count\n");
  }

  // ====== EffectSystem: stat modifier lifecycle (applied/removed with effect) ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{80};
    desc.name = SmallString{"might"};
    desc.durationType = EffectDurationType::duration;
    desc.duration = 5.0;

    StatModifier mod1{};
    mod1.statName = SmallString{"attack"};
    mod1.operation = StatModOp::additive;
    mod1.value = 25.0;
    desc.modifiers.push_back(mod1);

    StatModifier mod2{};
    mod2.statName = SmallString{"defense"};
    mod2.operation = StatModOp::multiplicative;
    mod2.value = 1.2;
    desc.modifiers.push_back(mod2);

    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    auto &sb = sys.stat_block(target);
    sb.set_base(SmallString{"attack"}, 100.0);
    sb.set_base(SmallString{"defense"}, 50.0);

    // Before effect
    assert(sb.compute(SmallString{"attack"}) == 100.0);
    assert(sb.compute(SmallString{"defense"}) == 50.0);

    // Apply effect
    auto instId = sys.apply_effect(EffectDescriptorId{80}, target);
    assert(instId.valid());

    // With effect active
    assert(sb.compute(SmallString{"attack"}) == 125.0);  // 100 + 25
    assert(std::abs(sb.compute(SmallString{"defense"}) - 60.0) < 1e-9);  // 50 * 1.2

    // Expire effect
    sys.tick(6.0);
    assert(sys.effect_count(target) == 0);

    // Modifiers removed
    assert(sb.compute(SmallString{"attack"}) == 100.0);
    assert(sb.compute(SmallString{"defense"}) == 50.0);

    fprintf(stderr, "[PASS] EffectSystem stat modifier lifecycle\n");
  }

  // ====== EffectSystem: has_granted_tag ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{90};
    desc.name = SmallString{"aura"};
    desc.durationType = EffectDurationType::infinite;
    desc.grantedTags.add(GameplayTag{SmallString{"status.protected"}});
    desc.grantedTags.add(GameplayTag{SmallString{"status.buffed"}});
    sys.register_descriptor(desc);

    GameplayEntityId target{1};
    assert(!sys.has_granted_tag(target, GameplayTag{SmallString{"status.protected"}}));

    auto instId = sys.apply_effect(EffectDescriptorId{90}, target);
    assert(instId.valid());

    assert(sys.has_granted_tag(target, GameplayTag{SmallString{"status.protected"}}));
    assert(sys.has_granted_tag(target, GameplayTag{SmallString{"status.buffed"}}));
    assert(!sys.has_granted_tag(target, GameplayTag{SmallString{"status.other"}}));

    // Remove effect -- tags should be gone
    sys.remove_effect(target, instId);
    assert(!sys.has_granted_tag(target, GameplayTag{SmallString{"status.protected"}}));

    fprintf(stderr, "[PASS] EffectSystem has_granted_tag\n");
  }

  // ====== EffectSystem: clear ======
  {
    EffectSystem sys{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{100};
    desc.name = SmallString{"test"};
    desc.durationType = EffectDurationType::infinite;
    sys.register_descriptor(desc);

    sys.apply_effect(EffectDescriptorId{100}, GameplayEntityId{1});
    sys.apply_effect(EffectDescriptorId{100}, GameplayEntityId{2});

    sys.clear();
    assert(sys.descriptor_count() == 0);
    assert(sys.total_effect_count() == 0);

    fprintf(stderr, "[PASS] EffectSystem clear\n");
  }

  // ====== EffectSystem: apply to non-existent descriptor ======
  {
    EffectSystem sys{};
    auto id = sys.apply_effect(EffectDescriptorId{999}, GameplayEntityId{1});
    assert(!id.valid());

    fprintf(stderr, "[PASS] EffectSystem apply non-existent descriptor\n");
  }

  // ====== EffectSystem: invalid descriptor registration ======
  {
    EffectSystem sys{};
    EffectDescriptor bad{};
    // id is 0, which is invalid
    assert(!sys.register_descriptor(bad));

    fprintf(stderr, "[PASS] EffectSystem invalid descriptor registration\n");
  }

  // ====== EffectDescriptor::from_definition ======
  {
    MechanicsDefinition def{};
    def.id = SmallString{"fire_dot_v2"};
    def.category = SmallString{"effect"};
    def.tags.add(GameplayTag{SmallString{"damage.fire"}});

    def.set_field(SmallString{"name"}, MechanicsFieldValue::make_string(SmallString{"Fire DoT v2"}));
    def.set_field(SmallString{"duration_type"}, MechanicsFieldValue::make_string(SmallString{"periodic"}));
    def.set_field(SmallString{"duration"}, MechanicsFieldValue::make_number(6.0));
    def.set_field(SmallString{"period"}, MechanicsFieldValue::make_number(2.0));
    def.set_field(SmallString{"magnitude"}, MechanicsFieldValue::make_number(15.0));
    def.set_field(SmallString{"max_stacks"}, MechanicsFieldValue::make_integer(5));
    def.set_field(SmallString{"stack_policy"}, MechanicsFieldValue::make_string(SmallString{"stack_count"}));

    auto desc = EffectDescriptor::from_definition(def, EffectDescriptorId{42});
    assert(desc.id == EffectDescriptorId{42});
    assert(desc.name == SmallString{"Fire DoT v2"});
    assert(desc.durationType == EffectDurationType::periodic);
    assert(desc.duration == 6.0);
    assert(desc.period == 2.0);
    assert(desc.magnitude == 15.0);
    assert(desc.maxStacks == 5);
    assert(desc.stackPolicy == EffectStackPolicy::stack_count);
    assert(desc.tags.has(GameplayTag{SmallString{"damage.fire"}}));

    fprintf(stderr, "[PASS] EffectDescriptor from_definition\n");
  }

  // ====== EffectSystem: periodic tick with stack_count multiplied magnitude ======
  {
    EffectSystem sys{};
    GameplayEventDispatcher dispatcher{};

    EffectDescriptor desc{};
    desc.id = EffectDescriptorId{110};
    desc.name = SmallString{"poison_stack"};
    desc.durationType = EffectDurationType::periodic;
    desc.duration = 10.0;
    desc.period = 1.0;
    desc.magnitude = 3.0;
    desc.stackPolicy = EffectStackPolicy::stack_count;
    desc.maxStacks = 5;
    sys.register_descriptor(desc);

    GameplayEntityId target{1};

    // Apply once
    sys.apply_effect(EffectDescriptorId{110}, target, {}, &dispatcher);
    // Stack to 3
    sys.apply_effect(EffectDescriptorId{110}, target);
    sys.apply_effect(EffectDescriptorId{110}, target);

    auto effects = sys.entity_effects(target);
    assert(effects.size() == 1);
    assert(effects[0]->stackCount == 3);

    // Tick 1.5s -- should fire 1 periodic tick
    dispatcher = GameplayEventDispatcher{};
    sys.tick(1.5, &dispatcher);

    // Find the periodic_tick event and check numericValue = 3.0 * 3 = 9.0
    bool foundTick = false;
    for (size_t i = 0; i < dispatcher.history().size(); ++i) {
      const auto &e = dispatcher.history()[i];
      if (e.typeId == effect_events::periodic_tick) {
        assert(std::abs(e.numericValue - 9.0) < 1e-9);
        foundTick = true;
      }
    }
    assert(foundTick);

    fprintf(stderr, "[PASS] EffectSystem periodic tick with stacks\n");
  }

  // ====== EffectSystem: find_stat_block ======
  {
    EffectSystem sys{};

    // No stat block for entity yet
    assert(sys.find_stat_block(GameplayEntityId{42}) == nullptr);

    // Create one via stat_block()
    sys.stat_block(GameplayEntityId{42}).set_base(SmallString{"hp"}, 100.0);

    const auto *sb = sys.find_stat_block(GameplayEntityId{42});
    assert(sb != nullptr);
    assert(sb->base(SmallString{"hp"}) == 100.0);

    fprintf(stderr, "[PASS] EffectSystem find_stat_block\n");
  }

  fprintf(stderr, "\n=== All EffectSystem tests passed ===\n");
  return 0;
}
