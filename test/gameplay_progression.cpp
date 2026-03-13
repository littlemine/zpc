#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "zensim/gameplay/GameplayProgression.hpp"

// ---- Helper: approximate floating-point comparison ----
static bool approx(double a, double b, double eps = 0.001) {
  return std::fabs(a - b) < eps;
}

// =====================================================================
//  Test 1: LevelThresholdTable - linear
// =====================================================================
static void test_threshold_linear() {
  fprintf(stderr, "[progression] test_threshold_linear...\n");

  auto table = zs::LevelThresholdTable::linear(100, 5);
  // Level 1: 0, Level 2: 100, Level 3: 200, Level 4: 300, Level 5: 400
  assert(table.maxLevel == 5);
  assert(table.thresholds.size() == 4);
  assert(table.thresholds[0] == 100);
  assert(table.thresholds[1] == 200);
  assert(table.thresholds[2] == 300);
  assert(table.thresholds[3] == 400);

  assert(table.level_for_xp(0) == 1);
  assert(table.level_for_xp(50) == 1);
  assert(table.level_for_xp(100) == 2);
  assert(table.level_for_xp(150) == 2);
  assert(table.level_for_xp(200) == 3);
  assert(table.level_for_xp(399) == 4);
  assert(table.level_for_xp(400) == 5);
  assert(table.level_for_xp(9999) == 5);  // capped at maxLevel

  assert(table.xp_for_level(1) == 0);
  assert(table.xp_for_level(2) == 100);
  assert(table.xp_for_level(3) == 200);
  assert(table.xp_for_level(5) == 400);

  fprintf(stderr, "[progression] test_threshold_linear PASS\n");
}

// =====================================================================
//  Test 2: LevelThresholdTable - quadratic
// =====================================================================
static void test_threshold_quadratic() {
  fprintf(stderr, "[progression] test_threshold_quadratic...\n");

  auto table = zs::LevelThresholdTable::quadratic(50, 4);
  // Level 1: 0, Level 2: 50*1=50, Level 3: 50*4=200, Level 4: 50*9=450
  assert(table.maxLevel == 4);
  assert(table.thresholds.size() == 3);
  assert(table.thresholds[0] == 50);
  assert(table.thresholds[1] == 200);
  assert(table.thresholds[2] == 450);

  assert(table.level_for_xp(0) == 1);
  assert(table.level_for_xp(49) == 1);
  assert(table.level_for_xp(50) == 2);
  assert(table.level_for_xp(199) == 2);
  assert(table.level_for_xp(200) == 3);
  assert(table.level_for_xp(449) == 3);
  assert(table.level_for_xp(450) == 4);

  fprintf(stderr, "[progression] test_threshold_quadratic PASS\n");
}

// =====================================================================
//  Test 3: LevelThresholdTable - from_thresholds
// =====================================================================
static void test_threshold_from_thresholds() {
  fprintf(stderr, "[progression] test_threshold_from_thresholds...\n");

  auto table = zs::LevelThresholdTable::from_thresholds({100, 300, 600, 1000});
  assert(table.maxLevel == 5);
  assert(table.level_for_xp(0) == 1);
  assert(table.level_for_xp(100) == 2);
  assert(table.level_for_xp(299) == 2);
  assert(table.level_for_xp(300) == 3);
  assert(table.level_for_xp(600) == 4);
  assert(table.level_for_xp(1000) == 5);
  assert(table.level_for_xp(5000) == 5);

  fprintf(stderr, "[progression] test_threshold_from_thresholds PASS\n");
}

// =====================================================================
//  Test 4: LevelThresholdTable - xp_to_next_level and level_progress
// =====================================================================
static void test_threshold_progress() {
  fprintf(stderr, "[progression] test_threshold_progress...\n");

  auto table = zs::LevelThresholdTable::linear(100, 5);

  // At 0 XP, level 1, need 100 to reach level 2
  assert(table.xp_to_next_level(0) == 100);
  assert(approx(table.level_progress(0), 0.0));

  // At 50 XP, level 1, need 50 more
  assert(table.xp_to_next_level(50) == 50);
  assert(approx(table.level_progress(50), 0.5));

  // At 100 XP, level 2, need 100 to reach level 3
  assert(table.xp_to_next_level(100) == 100);
  assert(approx(table.level_progress(100), 0.0));

  // At max level, xp_to_next is 0, progress is 1.0
  assert(table.xp_to_next_level(400) == 0);
  assert(approx(table.level_progress(400), 1.0));

  fprintf(stderr, "[progression] test_threshold_progress PASS\n");
}

// =====================================================================
//  Test 5: Progression IDs
// =====================================================================
static void test_progression_ids() {
  fprintf(stderr, "[progression] test_progression_ids...\n");

  zs::ProgressionProfileId p1{1}, p2{0};
  assert(p1.valid());
  assert(!p2.valid());
  assert(p1 != p2);

  zs::SkillNodeId s1{10}, s2{10};
  assert(s1 == s2);
  assert(s1.valid());

  zs::SkillTreeId t1{5};
  assert(t1.valid());

  fprintf(stderr, "[progression] test_progression_ids PASS\n");
}

// =====================================================================
//  Test 6: Enum names
// =====================================================================
static void test_enum_names() {
  fprintf(stderr, "[progression] test_enum_names...\n");

  assert(std::string(zs::stat_growth_type_name(zs::StatGrowthType::flat)) == "flat");
  assert(std::string(zs::stat_growth_type_name(zs::StatGrowthType::percentage)) == "percentage");
  assert(std::string(zs::stat_growth_type_name(zs::StatGrowthType::custom)) == "custom");

  assert(std::string(zs::skill_node_status_name(zs::SkillNodeStatus::locked)) == "locked");
  assert(std::string(zs::skill_node_status_name(zs::SkillNodeStatus::available)) == "available");
  assert(std::string(zs::skill_node_status_name(zs::SkillNodeStatus::unlocked)) == "unlocked");

  assert(std::string(zs::ProgressionSystem::unlock_result_name(
    zs::ProgressionSystem::UnlockResult::success)) == "success");
  assert(std::string(zs::ProgressionSystem::unlock_result_name(
    zs::ProgressionSystem::UnlockResult::prerequisites_not_met)) == "prerequisites_not_met");

  fprintf(stderr, "[progression] test_enum_names PASS\n");
}

// =====================================================================
//  Test 7: ProgressionProfile basics
// =====================================================================
static void test_profile_basics() {
  fprintf(stderr, "[progression] test_profile_basics...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  assert(prof.level == 1);
  assert(prof.totalXp == 0);
  assert(prof.skillPoints == 0);

  assert(sys.has_profile(zs::GameplayEntityId{1}));
  assert(!sys.has_profile(zs::GameplayEntityId{2}));
  assert(sys.profile_count() == 1);

  assert(sys.level(zs::GameplayEntityId{1}) == 1);
  assert(sys.total_xp(zs::GameplayEntityId{1}) == 0);
  assert(sys.level(zs::GameplayEntityId{999}) == 1);  // default for unknown

  fprintf(stderr, "[progression] test_profile_basics PASS\n");
}

// =====================================================================
//  Test 8: Basic XP award without level-up
// =====================================================================
static void test_xp_no_levelup() {
  fprintf(stderr, "[progression] test_xp_no_levelup...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::u32 gained = sys.award_xp(zs::GameplayEntityId{1}, 50);
  assert(gained == 0);
  assert(sys.level(zs::GameplayEntityId{1}) == 1);
  assert(sys.total_xp(zs::GameplayEntityId{1}) == 50);

  fprintf(stderr, "[progression] test_xp_no_levelup PASS\n");
}

// =====================================================================
//  Test 9: XP award with single level-up
// =====================================================================
static void test_xp_single_levelup() {
  fprintf(stderr, "[progression] test_xp_single_levelup...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::u32 gained = sys.award_xp(zs::GameplayEntityId{1}, 100);
  assert(gained == 1);
  assert(sys.level(zs::GameplayEntityId{1}) == 2);
  assert(sys.total_xp(zs::GameplayEntityId{1}) == 100);

  fprintf(stderr, "[progression] test_xp_single_levelup PASS\n");
}

// =====================================================================
//  Test 10: XP award with multi level-up
// =====================================================================
static void test_xp_multi_levelup() {
  fprintf(stderr, "[progression] test_xp_multi_levelup...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::u32 gained = sys.award_xp(zs::GameplayEntityId{1}, 350);
  assert(gained == 3);
  assert(sys.level(zs::GameplayEntityId{1}) == 4);
  assert(sys.total_xp(zs::GameplayEntityId{1}) == 350);

  fprintf(stderr, "[progression] test_xp_multi_levelup PASS\n");
}

// =====================================================================
//  Test 11: XP award capped at max level
// =====================================================================
static void test_xp_max_level() {
  fprintf(stderr, "[progression] test_xp_max_level...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 3));

  zs::u32 gained = sys.award_xp(zs::GameplayEntityId{1}, 9999);
  assert(gained == 2);  // Level 1 -> 3
  assert(sys.level(zs::GameplayEntityId{1}) == 3);

  // More XP, no more levels
  gained = sys.award_xp(zs::GameplayEntityId{1}, 1000);
  assert(gained == 0);
  assert(sys.level(zs::GameplayEntityId{1}) == 3);

  fprintf(stderr, "[progression] test_xp_max_level PASS\n");
}

// =====================================================================
//  Test 12: XP award events
// =====================================================================
static void test_xp_events() {
  fprintf(stderr, "[progression] test_xp_events...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::GameplayEventDispatcher dispatcher;
  dispatcher.set_history_capacity(50);

  zs::u32 gained = sys.award_xp(zs::GameplayEntityId{1}, 250, nullptr, &dispatcher);
  assert(gained == 2);

  // Should have: 1 xp_gained, 2 skill_points_gained, 2 level_up
  auto &hist = dispatcher.history();
  assert(hist.size() == 5);

  // First: xp_gained
  assert(hist[0].typeId == zs::progression_events::XP_GAINED);
  assert(approx(hist[0].numericValue, 250.0));

  // Skill points events
  assert(hist[1].typeId == zs::progression_events::SKILL_POINTS_GAINED);
  assert(hist[2].typeId == zs::progression_events::SKILL_POINTS_GAINED);

  // Level-up events
  assert(hist[3].typeId == zs::progression_events::LEVEL_UP);
  assert(approx(hist[3].numericValue, 2.0));
  assert(hist[4].typeId == zs::progression_events::LEVEL_UP);
  assert(approx(hist[4].numericValue, 3.0));

  fprintf(stderr, "[progression] test_xp_events PASS\n");
}

// =====================================================================
//  Test 13: Skill points per level
// =====================================================================
static void test_skill_points() {
  fprintf(stderr, "[progression] test_skill_points...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));
  sys.set_skill_points_per_level(2);

  sys.award_xp(zs::GameplayEntityId{1}, 300);
  // Level 1 -> 4, gained levels 2,3,4 = 3 levels * 2 points = 6 points
  auto *prof = sys.find_profile(zs::GameplayEntityId{1});
  assert(prof != nullptr);
  assert(prof->skillPoints == 6);
  assert(prof->totalSkillPoints == 6);

  fprintf(stderr, "[progression] test_skill_points PASS\n");
}

// =====================================================================
//  Test 14: Custom skill points function
// =====================================================================
static void test_custom_skill_points() {
  fprintf(stderr, "[progression] test_custom_skill_points...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));
  // Award level number of points at each level
  sys.set_skill_points_function([](zs::u32 level) -> zs::u32 { return level; });

  sys.award_xp(zs::GameplayEntityId{1}, 300);
  // Level 2: 2pts, Level 3: 3pts, Level 4: 4pts = 9 total
  auto *prof = sys.find_profile(zs::GameplayEntityId{1});
  assert(prof != nullptr);
  assert(prof->skillPoints == 9);

  fprintf(stderr, "[progression] test_custom_skill_points PASS\n");
}

// =====================================================================
//  Test 15: Set level directly
// =====================================================================
static void test_set_level() {
  fprintf(stderr, "[progression] test_set_level...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  sys.set_level(zs::GameplayEntityId{1}, 5);
  assert(sys.level(zs::GameplayEntityId{1}) == 5);
  assert(sys.total_xp(zs::GameplayEntityId{1}) == 400);

  fprintf(stderr, "[progression] test_set_level PASS\n");
}

// =====================================================================
//  Test 16: Flat stat growth
// =====================================================================
static void test_flat_stat_growth() {
  fprintf(stderr, "[progression] test_flat_stat_growth...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::StatGrowthTable gt;
  gt.add_flat(zs::SmallString{"strength"}, 5.0);
  gt.add_flat(zs::SmallString{"vitality"}, 3.0);
  sys.register_growth_table(zs::SmallString{"warrior"}, gt);
  sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"warrior"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"strength"}, 10.0);
  stats.set_base(zs::SmallString{"vitality"}, 20.0);

  // At level 1, no growth applied
  sys.set_level(zs::GameplayEntityId{1}, 1, &stats);
  assert(approx(stats.compute(zs::SmallString{"strength"}), 10.0));
  assert(approx(stats.compute(zs::SmallString{"vitality"}), 20.0));

  // At level 5: growth = perLevel * (5-1) = 5*4 = 20 strength, 3*4 = 12 vitality
  sys.set_level(zs::GameplayEntityId{1}, 5, &stats);
  assert(approx(stats.compute(zs::SmallString{"strength"}), 30.0));  // 10 + 20
  assert(approx(stats.compute(zs::SmallString{"vitality"}), 32.0));  // 20 + 12

  fprintf(stderr, "[progression] test_flat_stat_growth PASS\n");
}

// =====================================================================
//  Test 17: Percentage stat growth
// =====================================================================
static void test_percentage_stat_growth() {
  fprintf(stderr, "[progression] test_percentage_stat_growth...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::StatGrowthTable gt;
  gt.add_percentage(zs::SmallString{"speed"}, 0.1);  // +10% per level
  sys.register_growth_table(zs::SmallString{"rogue"}, gt);
  sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"rogue"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"speed"}, 100.0);

  // Level 3: multiplier = 1 + 0.1 * (3-1) = 1.2
  sys.set_level(zs::GameplayEntityId{1}, 3, &stats);
  assert(approx(stats.compute(zs::SmallString{"speed"}), 120.0));

  fprintf(stderr, "[progression] test_percentage_stat_growth PASS\n");
}

// =====================================================================
//  Test 18: Custom stat growth function
// =====================================================================
static void test_custom_stat_growth() {
  fprintf(stderr, "[progression] test_custom_stat_growth...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::StatGrowthTable gt;
  // Custom: add base * 0.5 * (level-1) (effectively +50% of base per level)
  gt.add_custom(zs::SmallString{"magic"}, [](double base, zs::u32 level) -> double {
    return base * 0.5 * static_cast<double>(level - 1);
  });
  sys.register_growth_table(zs::SmallString{"mage"}, gt);
  sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"mage"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"magic"}, 20.0);

  // Level 4: custom(20, 4) = 20 * 0.5 * 3 = 30 additive
  sys.set_level(zs::GameplayEntityId{1}, 4, &stats);
  assert(approx(stats.compute(zs::SmallString{"magic"}), 50.0));  // 20 + 30

  fprintf(stderr, "[progression] test_custom_stat_growth PASS\n");
}

// =====================================================================
//  Test 19: Stat growth updates on level-up via award_xp
// =====================================================================
static void test_stat_growth_on_levelup() {
  fprintf(stderr, "[progression] test_stat_growth_on_levelup...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::StatGrowthTable gt;
  gt.add_flat(zs::SmallString{"attack"}, 10.0);
  sys.register_growth_table(zs::SmallString{"fighter"}, gt);
  sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"fighter"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"attack"}, 50.0);

  // Level up to 3 via XP
  zs::u32 gained = sys.award_xp(zs::GameplayEntityId{1}, 200, &stats);
  assert(gained == 2);
  // Level 3: attack = 50 + 10 * (3-1) = 70
  assert(approx(stats.compute(zs::SmallString{"attack"}), 70.0));

  // Level up again to 5
  gained = sys.award_xp(zs::GameplayEntityId{1}, 200, &stats);
  assert(gained == 2);
  // Level 5: attack = 50 + 10 * (5-1) = 90
  assert(approx(stats.compute(zs::SmallString{"attack"}), 90.0));

  fprintf(stderr, "[progression] test_stat_growth_on_levelup PASS\n");
}

// =====================================================================
//  Test 20: Skill node registration and queries
// =====================================================================
static void test_skill_node_registration() {
  fprintf(stderr, "[progression] test_skill_node_registration...\n");

  zs::ProgressionSystem sys;

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{100};
  node1.name = zs::SmallString{"Fireball"};
  node1.requiredLevel = 2;
  node1.pointCost = 1;
  assert(sys.register_skill_node(node1));

  zs::SkillNodeDescriptor node2;
  node2.id = zs::SkillNodeId{2};
  node2.treeId = zs::SkillTreeId{100};
  node2.name = zs::SmallString{"Meteor"};
  node2.requiredLevel = 5;
  node2.pointCost = 3;
  node2.prerequisites.push_back(zs::SkillNodeId{1});
  assert(sys.register_skill_node(node2));

  // Duplicate ID
  assert(!sys.register_skill_node(node1));

  // Invalid ID
  zs::SkillNodeDescriptor bad;
  bad.id = zs::SkillNodeId{0};
  assert(!sys.register_skill_node(bad));

  assert(sys.skill_node_count() == 2);

  auto *found = sys.find_skill_node(zs::SkillNodeId{1});
  assert(found != nullptr);
  assert(std::string(found->name.asChars()) == "Fireball");

  assert(sys.find_skill_node(zs::SkillNodeId{999}) == nullptr);

  auto treeNodes = sys.nodes_for_tree(zs::SkillTreeId{100});
  assert(treeNodes.size() == 2);

  fprintf(stderr, "[progression] test_skill_node_registration PASS\n");
}

// =====================================================================
//  Test 21: Skill node status checking
// =====================================================================
static void test_skill_node_status() {
  fprintf(stderr, "[progression] test_skill_node_status...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{1};
  node1.name = zs::SmallString{"Slash"};
  node1.requiredLevel = 1;
  node1.pointCost = 1;
  sys.register_skill_node(node1);

  zs::SkillNodeDescriptor node2;
  node2.id = zs::SkillNodeId{2};
  node2.treeId = zs::SkillTreeId{1};
  node2.name = zs::SmallString{"PowerSlash"};
  node2.requiredLevel = 3;
  node2.pointCost = 2;
  node2.prerequisites.push_back(zs::SkillNodeId{1});
  sys.register_skill_node(node2);

  // Create entity at level 1
  sys.profile(zs::GameplayEntityId{1});

  // Node 1: available (level 1 requirement met, no prereqs)
  assert(sys.node_status(zs::GameplayEntityId{1}, zs::SkillNodeId{1})
         == zs::SkillNodeStatus::available);

  // Node 2: locked (level too low AND prerequisite not met)
  assert(sys.node_status(zs::GameplayEntityId{1}, zs::SkillNodeId{2})
         == zs::SkillNodeStatus::locked);

  // Non-existent node
  assert(sys.node_status(zs::GameplayEntityId{1}, zs::SkillNodeId{999})
         == zs::SkillNodeStatus::locked);

  fprintf(stderr, "[progression] test_skill_node_status PASS\n");
}

// =====================================================================
//  Test 22: Skill unlock - success
// =====================================================================
static void test_skill_unlock_success() {
  fprintf(stderr, "[progression] test_skill_unlock_success...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{1};
  node1.name = zs::SmallString{"Bash"};
  node1.requiredLevel = 1;
  node1.pointCost = 1;
  sys.register_skill_node(node1);

  // Give entity points via level-up
  sys.award_xp(zs::GameplayEntityId{1}, 200);  // Level 3, 2 skill points
  auto *prof = sys.find_profile(zs::GameplayEntityId{1});
  assert(prof != nullptr);
  assert(prof->skillPoints == 2);

  auto result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1});
  assert(result == zs::ProgressionSystem::UnlockResult::success);

  prof = sys.find_profile(zs::GameplayEntityId{1});
  assert(prof->skillPoints == 1);  // Spent 1
  assert(prof->is_skill_unlocked(zs::SkillNodeId{1}));

  assert(sys.node_status(zs::GameplayEntityId{1}, zs::SkillNodeId{1})
         == zs::SkillNodeStatus::unlocked);

  fprintf(stderr, "[progression] test_skill_unlock_success PASS\n");
}

// =====================================================================
//  Test 23: Skill unlock - failures
// =====================================================================
static void test_skill_unlock_failures() {
  fprintf(stderr, "[progression] test_skill_unlock_failures...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{1};
  node1.name = zs::SmallString{"BasicAttack"};
  node1.requiredLevel = 3;
  node1.pointCost = 2;
  sys.register_skill_node(node1);

  // Entity at level 1 with 0 points
  sys.profile(zs::GameplayEntityId{1});

  // Insufficient level
  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 10;  // Give plenty of points
  auto result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1});
  assert(result == zs::ProgressionSystem::UnlockResult::insufficient_level);

  // Level up to 3, but remove points
  sys.set_level(zs::GameplayEntityId{1}, 3);
  sys.profile(zs::GameplayEntityId{1}).skillPoints = 0;
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1});
  assert(result == zs::ProgressionSystem::UnlockResult::insufficient_points);

  // Node not found
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{999});
  assert(result == zs::ProgressionSystem::UnlockResult::node_not_found);

  // Profile not found
  result = sys.unlock_skill(zs::GameplayEntityId{999}, zs::SkillNodeId{1});
  assert(result == zs::ProgressionSystem::UnlockResult::profile_not_found);

  fprintf(stderr, "[progression] test_skill_unlock_failures PASS\n");
}

// =====================================================================
//  Test 24: Skill unlock - prerequisite gating
// =====================================================================
static void test_skill_prerequisites() {
  fprintf(stderr, "[progression] test_skill_prerequisites...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{1};
  node1.name = zs::SmallString{"Base"};
  node1.requiredLevel = 1;
  node1.pointCost = 1;
  sys.register_skill_node(node1);

  zs::SkillNodeDescriptor node2;
  node2.id = zs::SkillNodeId{2};
  node2.treeId = zs::SkillTreeId{1};
  node2.name = zs::SmallString{"Advanced"};
  node2.requiredLevel = 1;
  node2.pointCost = 1;
  node2.prerequisites.push_back(zs::SkillNodeId{1});
  sys.register_skill_node(node2);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 10;

  // Can't unlock node2 without node1
  auto result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{2});
  assert(result == zs::ProgressionSystem::UnlockResult::prerequisites_not_met);

  // Unlock node1 first
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1});
  assert(result == zs::ProgressionSystem::UnlockResult::success);

  // Now node2 should work
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{2});
  assert(result == zs::ProgressionSystem::UnlockResult::success);

  fprintf(stderr, "[progression] test_skill_prerequisites PASS\n");
}

// =====================================================================
//  Test 25: Multi-rank skill nodes
// =====================================================================
static void test_multi_rank_skills() {
  fprintf(stderr, "[progression] test_multi_rank_skills...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node;
  node.id = zs::SkillNodeId{1};
  node.treeId = zs::SkillTreeId{1};
  node.name = zs::SmallString{"Toughness"};
  node.requiredLevel = 1;
  node.pointCost = 1;
  node.maxRanks = 3;
  node.statBonuses.push_back({zs::SmallString{"hp"}, 10.0});
  sys.register_skill_node(node);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 10;

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"hp"}, 100.0);

  // Rank 1
  auto result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1}, &stats);
  assert(result == zs::ProgressionSystem::UnlockResult::success);
  assert(prof.skill_rank(zs::SkillNodeId{1}) == 1);
  assert(approx(stats.compute(zs::SmallString{"hp"}), 110.0));

  // Rank 2
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1}, &stats);
  assert(result == zs::ProgressionSystem::UnlockResult::success);
  assert(prof.skill_rank(zs::SkillNodeId{1}) == 2);
  // Rank 2: bonus = 10 * 1 (rank1) + 10 * 2 (rank2) = 30 total
  assert(approx(stats.compute(zs::SmallString{"hp"}), 130.0));

  // Rank 3
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1}, &stats);
  assert(result == zs::ProgressionSystem::UnlockResult::success);
  assert(prof.skill_rank(zs::SkillNodeId{1}) == 3);

  // Rank 4 should fail
  result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1}, &stats);
  assert(result == zs::ProgressionSystem::UnlockResult::already_max_rank);

  fprintf(stderr, "[progression] test_multi_rank_skills PASS\n");
}

// =====================================================================
//  Test 26: Skill unlock events
// =====================================================================
static void test_skill_unlock_events() {
  fprintf(stderr, "[progression] test_skill_unlock_events...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node;
  node.id = zs::SkillNodeId{1};
  node.treeId = zs::SkillTreeId{1};
  node.name = zs::SmallString{"PowerStrike"};
  node.requiredLevel = 1;
  node.pointCost = 1;
  node.maxRanks = 2;
  sys.register_skill_node(node);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 10;

  zs::GameplayEventDispatcher dispatcher;
  dispatcher.set_history_capacity(20);

  // First unlock
  sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1},
                   nullptr, nullptr, &dispatcher);
  assert(dispatcher.history().size() == 1);
  assert(dispatcher.history()[0].typeId == zs::progression_events::SKILL_UNLOCKED);
  assert(std::string(dispatcher.history()[0].stringValue.asChars()) == "PowerStrike");
  assert(approx(dispatcher.history()[0].numericValue, 1.0));

  // Second rank
  sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1},
                   nullptr, nullptr, &dispatcher);
  assert(dispatcher.history().size() == 2);
  assert(dispatcher.history()[1].typeId == zs::progression_events::SKILL_RANK_UP);
  assert(approx(dispatcher.history()[1].numericValue, 2.0));

  fprintf(stderr, "[progression] test_skill_unlock_events PASS\n");
}

// =====================================================================
//  Test 27: Skill stat bonuses
// =====================================================================
static void test_skill_stat_bonuses() {
  fprintf(stderr, "[progression] test_skill_stat_bonuses...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node;
  node.id = zs::SkillNodeId{1};
  node.treeId = zs::SkillTreeId{1};
  node.name = zs::SmallString{"IronSkin"};
  node.requiredLevel = 1;
  node.pointCost = 1;
  node.statBonuses.push_back({zs::SmallString{"armor"}, 5.0});
  node.statBonuses.push_back({zs::SmallString{"hp"}, 20.0});
  sys.register_skill_node(node);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 5;

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"armor"}, 10.0);
  stats.set_base(zs::SmallString{"hp"}, 100.0);

  sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1}, &stats);
  assert(approx(stats.compute(zs::SmallString{"armor"}), 15.0));
  assert(approx(stats.compute(zs::SmallString{"hp"}), 120.0));

  fprintf(stderr, "[progression] test_skill_stat_bonuses PASS\n");
}

// =====================================================================
//  Test 28: Skill reset
// =====================================================================
static void test_skill_reset() {
  fprintf(stderr, "[progression] test_skill_reset...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{1};
  node1.name = zs::SmallString{"A"};
  node1.requiredLevel = 1;
  node1.pointCost = 2;
  node1.statBonuses.push_back({zs::SmallString{"str"}, 5.0});
  sys.register_skill_node(node1);

  zs::SkillNodeDescriptor node2;
  node2.id = zs::SkillNodeId{2};
  node2.treeId = zs::SkillTreeId{1};
  node2.name = zs::SmallString{"B"};
  node2.requiredLevel = 1;
  node2.pointCost = 3;
  sys.register_skill_node(node2);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 10;

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"str"}, 10.0);

  sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1}, &stats);
  sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{2}, &stats);
  assert(prof.skillPoints == 5);  // 10 - 2 - 3
  assert(approx(stats.compute(zs::SmallString{"str"}), 15.0));  // 10 + 5

  zs::u32 refunded = sys.reset_skills(zs::GameplayEntityId{1}, &stats);
  assert(refunded == 5);  // 2 + 3
  assert(prof.skillPoints == 10);  // 5 + 5 refunded
  assert(!prof.is_skill_unlocked(zs::SkillNodeId{1}));
  assert(!prof.is_skill_unlocked(zs::SkillNodeId{2}));
  assert(approx(stats.compute(zs::SmallString{"str"}), 10.0));  // bonuses removed

  fprintf(stderr, "[progression] test_skill_reset PASS\n");
}

// =====================================================================
//  Test 29: Available and unlocked skill queries
// =====================================================================
static void test_skill_queries() {
  fprintf(stderr, "[progression] test_skill_queries...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::SkillNodeDescriptor node1;
  node1.id = zs::SkillNodeId{1};
  node1.treeId = zs::SkillTreeId{1};
  node1.name = zs::SmallString{"A"};
  node1.requiredLevel = 1;
  node1.pointCost = 1;
  sys.register_skill_node(node1);

  zs::SkillNodeDescriptor node2;
  node2.id = zs::SkillNodeId{2};
  node2.treeId = zs::SkillTreeId{1};
  node2.name = zs::SmallString{"B"};
  node2.requiredLevel = 1;
  node2.pointCost = 1;
  node2.prerequisites.push_back(zs::SkillNodeId{1});
  sys.register_skill_node(node2);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 10;

  auto available = sys.available_skills(zs::GameplayEntityId{1});
  assert(available.size() == 1);  // Only node1 available
  assert(available[0] == zs::SkillNodeId{1});

  auto unlocked = sys.unlocked_skills(zs::GameplayEntityId{1});
  assert(unlocked.empty());

  // Unlock node1
  sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1});

  unlocked = sys.unlocked_skills(zs::GameplayEntityId{1});
  assert(unlocked.size() == 1);

  available = sys.available_skills(zs::GameplayEntityId{1});
  assert(available.size() == 1);  // Now node2 is available
  assert(available[0] == zs::SkillNodeId{2});

  fprintf(stderr, "[progression] test_skill_queries PASS\n");
}

// =====================================================================
//  Test 30: Growth table management
// =====================================================================
static void test_growth_table_management() {
  fprintf(stderr, "[progression] test_growth_table_management...\n");

  zs::ProgressionSystem sys;

  zs::StatGrowthTable gt1;
  gt1.add_flat(zs::SmallString{"str"}, 5.0);
  sys.register_growth_table(zs::SmallString{"warrior"}, gt1);

  zs::StatGrowthTable gt2;
  gt2.add_flat(zs::SmallString{"int"}, 8.0);
  sys.register_growth_table(zs::SmallString{"mage"}, gt2);

  assert(sys.growth_table_count() == 2);

  assert(sys.find_growth_table(zs::SmallString{"warrior"}) != nullptr);
  assert(sys.find_growth_table(zs::SmallString{"mage"}) != nullptr);
  assert(sys.find_growth_table(zs::SmallString{"rogue"}) == nullptr);

  // Assign non-existent table
  assert(!sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"rogue"}));
  assert(sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"warrior"}));

  fprintf(stderr, "[progression] test_growth_table_management PASS\n");
}

// =====================================================================
//  Test 31: Remove profile
// =====================================================================
static void test_remove_profile() {
  fprintf(stderr, "[progression] test_remove_profile...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  sys.profile(zs::GameplayEntityId{1});
  sys.profile(zs::GameplayEntityId{2});
  assert(sys.profile_count() == 2);

  assert(sys.remove_profile(zs::GameplayEntityId{1}));
  assert(sys.profile_count() == 1);
  assert(!sys.has_profile(zs::GameplayEntityId{1}));
  assert(sys.has_profile(zs::GameplayEntityId{2}));

  assert(!sys.remove_profile(zs::GameplayEntityId{999}));

  fprintf(stderr, "[progression] test_remove_profile PASS\n");
}

// =====================================================================
//  Test 32: System clear
// =====================================================================
static void test_system_clear() {
  fprintf(stderr, "[progression] test_system_clear...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::StatGrowthTable gt;
  gt.add_flat(zs::SmallString{"str"}, 5.0);
  sys.register_growth_table(zs::SmallString{"warrior"}, gt);

  zs::SkillNodeDescriptor node;
  node.id = zs::SkillNodeId{1};
  node.treeId = zs::SkillTreeId{1};
  node.name = zs::SmallString{"A"};
  sys.register_skill_node(node);

  sys.profile(zs::GameplayEntityId{1});

  sys.clear();
  assert(sys.profile_count() == 0);
  assert(sys.skill_node_count() == 0);
  assert(sys.growth_table_count() == 0);

  fprintf(stderr, "[progression] test_system_clear PASS\n");
}

// =====================================================================
//  Test 33: Clear profiles only
// =====================================================================
static void test_clear_profiles() {
  fprintf(stderr, "[progression] test_clear_profiles...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::SkillNodeDescriptor node;
  node.id = zs::SkillNodeId{1};
  node.treeId = zs::SkillTreeId{1};
  node.name = zs::SmallString{"A"};
  sys.register_skill_node(node);

  sys.profile(zs::GameplayEntityId{1});

  sys.clear_profiles();
  assert(sys.profile_count() == 0);
  assert(sys.skill_node_count() == 1);  // Config preserved

  fprintf(stderr, "[progression] test_clear_profiles PASS\n");
}

// =====================================================================
//  Test 34: Integrated scenario - full progression lifecycle
// =====================================================================
static void test_integrated_lifecycle() {
  fprintf(stderr, "[progression] test_integrated_lifecycle...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::from_thresholds({100, 300, 600, 1000}));
  sys.set_skill_points_per_level(2);  // 2 points per level to fund skills

  // Growth table
  zs::StatGrowthTable gt;
  gt.add_flat(zs::SmallString{"attack"}, 5.0);
  gt.add_flat(zs::SmallString{"defense"}, 3.0);
  gt.add_percentage(zs::SmallString{"hp"}, 0.1);
  sys.register_growth_table(zs::SmallString{"knight"}, gt);

  // Skill tree
  zs::SkillNodeDescriptor skill1;
  skill1.id = zs::SkillNodeId{1};
  skill1.treeId = zs::SkillTreeId{1};
  skill1.name = zs::SmallString{"ShieldBash"};
  skill1.requiredLevel = 1;
  skill1.pointCost = 1;
  skill1.statBonuses.push_back({zs::SmallString{"defense"}, 2.0});
  sys.register_skill_node(skill1);

  zs::SkillNodeDescriptor skill2;
  skill2.id = zs::SkillNodeId{2};
  skill2.treeId = zs::SkillTreeId{1};
  skill2.name = zs::SmallString{"FortifiedStance"};
  skill2.requiredLevel = 3;
  skill2.pointCost = 2;
  skill2.prerequisites.push_back(zs::SkillNodeId{1});
  skill2.statBonuses.push_back({zs::SmallString{"defense"}, 5.0});
  skill2.statBonuses.push_back({zs::SmallString{"hp"}, 50.0});
  sys.register_skill_node(skill2);

  // Setup entity
  zs::GameplayEntityId entity{1};
  sys.assign_growth_table(entity, zs::SmallString{"knight"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"attack"}, 20.0);
  stats.set_base(zs::SmallString{"defense"}, 10.0);
  stats.set_base(zs::SmallString{"hp"}, 100.0);

  zs::GameplayEventDispatcher dispatcher;
  dispatcher.set_history_capacity(100);

  // Gain XP to level 2
  zs::u32 gained = sys.award_xp(entity, 100, &stats, &dispatcher);
  assert(gained == 1);
  assert(sys.level(entity) == 2);
  // attack: 20 + 5*(2-1) = 25
  assert(approx(stats.compute(zs::SmallString{"attack"}), 25.0));
  // defense: 10 + 3*(2-1) = 13
  assert(approx(stats.compute(zs::SmallString{"defense"}), 13.0));
  // hp: 100 * (1 + 0.1*(2-1)) = 110
  assert(approx(stats.compute(zs::SmallString{"hp"}), 110.0));

  // Unlock skill 1 (has points from level up)
  auto result = sys.unlock_skill(entity, zs::SkillNodeId{1}, &stats, nullptr, &dispatcher);
  assert(result == zs::ProgressionSystem::UnlockResult::success);
  // defense: 10 + 3 (growth) + 2 (skill) = 15
  assert(approx(stats.compute(zs::SmallString{"defense"}), 15.0));

  // Can't unlock skill 2 yet (need level 3)
  result = sys.unlock_skill(entity, zs::SkillNodeId{2});
  assert(result == zs::ProgressionSystem::UnlockResult::insufficient_level);

  // Level up to 3
  gained = sys.award_xp(entity, 200, &stats, &dispatcher);
  assert(gained == 1);
  assert(sys.level(entity) == 3);
  // attack: 20 + 5*(3-1) = 30
  assert(approx(stats.compute(zs::SmallString{"attack"}), 30.0));

  // Now unlock skill 2
  result = sys.unlock_skill(entity, zs::SkillNodeId{2}, &stats, nullptr, &dispatcher);
  assert(result == zs::ProgressionSystem::UnlockResult::success);
  // defense: 10 + 3*(3-1) + 2 (skill1) + 5 (skill2) = 10 + 6 + 2 + 5 = 23
  assert(approx(stats.compute(zs::SmallString{"defense"}), 23.0));

  // Verify events were emitted (xp, skill_points, level_up, skill_unlocked)
  bool hasXpEvent = false, hasLevelUp = false, hasSkillUnlock = false;
  for (auto &evt : dispatcher.history()) {
    if (evt.typeId == zs::progression_events::XP_GAINED) hasXpEvent = true;
    if (evt.typeId == zs::progression_events::LEVEL_UP) hasLevelUp = true;
    if (evt.typeId == zs::progression_events::SKILL_UNLOCKED) hasSkillUnlock = true;
  }
  assert(hasXpEvent);
  assert(hasLevelUp);
  assert(hasSkillUnlock);

  fprintf(stderr, "[progression] test_integrated_lifecycle PASS\n");
}

// =====================================================================
//  Test 35: Multiple entities with different growth tables
// =====================================================================
static void test_multiple_entities() {
  fprintf(stderr, "[progression] test_multiple_entities...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::StatGrowthTable warriorGt;
  warriorGt.add_flat(zs::SmallString{"str"}, 8.0);
  sys.register_growth_table(zs::SmallString{"warrior"}, warriorGt);

  zs::StatGrowthTable mageGt;
  mageGt.add_flat(zs::SmallString{"int"}, 10.0);
  sys.register_growth_table(zs::SmallString{"mage"}, mageGt);

  zs::GameplayEntityId warrior{1}, mage{2};
  sys.assign_growth_table(warrior, zs::SmallString{"warrior"});
  sys.assign_growth_table(mage, zs::SmallString{"mage"});

  zs::StatBlock warriorStats, mageStats;
  warriorStats.set_base(zs::SmallString{"str"}, 15.0);
  mageStats.set_base(zs::SmallString{"int"}, 12.0);

  sys.set_level(warrior, 5, &warriorStats);
  sys.set_level(mage, 3, &mageStats);

  // Warrior: str = 15 + 8*(5-1) = 15 + 32 = 47
  assert(approx(warriorStats.compute(zs::SmallString{"str"}), 47.0));

  // Mage: int = 12 + 10*(3-1) = 12 + 20 = 32
  assert(approx(mageStats.compute(zs::SmallString{"int"}), 32.0));

  assert(sys.profile_count() == 2);

  fprintf(stderr, "[progression] test_multiple_entities PASS\n");
}

// =====================================================================
//  Test 36: Stat growth reapplication (old modifiers removed)
// =====================================================================
static void test_stat_growth_reapplication() {
  fprintf(stderr, "[progression] test_stat_growth_reapplication...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 20));

  zs::StatGrowthTable gt;
  gt.add_flat(zs::SmallString{"str"}, 5.0);
  sys.register_growth_table(zs::SmallString{"test"}, gt);
  sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"test"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"str"}, 10.0);

  // Level 3: 10 + 5*2 = 20
  sys.set_level(zs::GameplayEntityId{1}, 3, &stats);
  assert(approx(stats.compute(zs::SmallString{"str"}), 20.0));

  // Level 10: 10 + 5*9 = 55 (old modifier at level 3 should be replaced)
  sys.set_level(zs::GameplayEntityId{1}, 10, &stats);
  assert(approx(stats.compute(zs::SmallString{"str"}), 55.0));

  // Back to level 1: no growth modifier
  sys.set_level(zs::GameplayEntityId{1}, 1, &stats);
  assert(approx(stats.compute(zs::SmallString{"str"}), 10.0));

  fprintf(stderr, "[progression] test_stat_growth_reapplication PASS\n");
}

// =====================================================================
//  Test 37: Edge case - single level table
// =====================================================================
static void test_single_level_table() {
  fprintf(stderr, "[progression] test_single_level_table...\n");

  auto table = zs::LevelThresholdTable::linear(100, 1);
  assert(table.maxLevel == 1);
  assert(table.thresholds.empty());
  assert(table.level_for_xp(0) == 1);
  assert(table.level_for_xp(9999) == 1);
  assert(table.xp_to_next_level(0) == 0);
  assert(approx(table.level_progress(0), 1.0));

  fprintf(stderr, "[progression] test_single_level_table PASS\n");
}

// =====================================================================
//  Test 38: Skill unlock with granted effect
// =====================================================================
static void test_skill_granted_effect() {
  fprintf(stderr, "[progression] test_skill_granted_effect...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  // Set up effect system with a descriptor
  zs::EffectSystem effectSys;
  zs::EffectDescriptor effectDesc;
  effectDesc.id = zs::EffectDescriptorId{50};
  effectDesc.name = zs::SmallString{"SkillBuff"};
  effectDesc.durationType = zs::EffectDurationType::infinite;
  effectSys.register_descriptor(effectDesc);

  zs::SkillNodeDescriptor node;
  node.id = zs::SkillNodeId{1};
  node.treeId = zs::SkillTreeId{1};
  node.name = zs::SmallString{"PassiveBuff"};
  node.requiredLevel = 1;
  node.pointCost = 1;
  node.grantedEffect = zs::EffectDescriptorId{50};
  sys.register_skill_node(node);

  auto &prof = sys.profile(zs::GameplayEntityId{1});
  prof.skillPoints = 5;

  // Ensure the effect system has a stat block for this entity
  effectSys.stat_block(zs::GameplayEntityId{1}).set_base(zs::SmallString{"power"}, 10.0);

  auto result = sys.unlock_skill(zs::GameplayEntityId{1}, zs::SkillNodeId{1},
                                 nullptr, &effectSys, nullptr);
  assert(result == zs::ProgressionSystem::UnlockResult::success);

  // Verify effect was applied
  assert(effectSys.has_effect(zs::GameplayEntityId{1}, zs::EffectDescriptorId{50}));

  fprintf(stderr, "[progression] test_skill_granted_effect PASS\n");
}

// =====================================================================
//  Test 39: Combined growth + equipment stat interaction
// =====================================================================
static void test_growth_and_external_modifiers() {
  fprintf(stderr, "[progression] test_growth_and_external_modifiers...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 10));

  zs::StatGrowthTable gt;
  gt.add_flat(zs::SmallString{"attack"}, 5.0);
  sys.register_growth_table(zs::SmallString{"fighter"}, gt);
  sys.assign_growth_table(zs::GameplayEntityId{1}, zs::SmallString{"fighter"});

  zs::StatBlock stats;
  stats.set_base(zs::SmallString{"attack"}, 20.0);

  // Add an "equipment" modifier separately
  zs::StatModifier equipMod;
  equipMod.statName = zs::SmallString{"attack"};
  equipMod.operation = zs::StatModOp::additive;
  equipMod.value = 15.0;
  equipMod.sourceEffect = zs::EffectInstanceId{12345};  // Some equipment source
  equipMod.priority = 0;
  stats.add_modifier(equipMod);

  // Level up to 4
  sys.set_level(zs::GameplayEntityId{1}, 4, &stats);
  // attack = 20 (base) + 5*3 (growth) + 15 (equip) = 50
  assert(approx(stats.compute(zs::SmallString{"attack"}), 50.0));

  // Level up to 6 — growth reapplied but equipment stays
  sys.set_level(zs::GameplayEntityId{1}, 6, &stats);
  // attack = 20 + 5*5 (growth) + 15 (equip) = 60
  assert(approx(stats.compute(zs::SmallString{"attack"}), 60.0));

  fprintf(stderr, "[progression] test_growth_and_external_modifiers PASS\n");
}

// =====================================================================
//  Test 40: XP events include correct source/target
// =====================================================================
static void test_event_entity_ids() {
  fprintf(stderr, "[progression] test_event_entity_ids...\n");

  zs::ProgressionSystem sys;
  sys.set_threshold_table(zs::LevelThresholdTable::linear(100, 5));

  zs::GameplayEventDispatcher dispatcher;
  dispatcher.set_history_capacity(20);

  sys.award_xp(zs::GameplayEntityId{42}, 150, nullptr, &dispatcher);

  for (auto &evt : dispatcher.history()) {
    assert(evt.source == zs::GameplayEntityId{42});
    assert(evt.target == zs::GameplayEntityId{42});
  }

  fprintf(stderr, "[progression] test_event_entity_ids PASS\n");
}

// =====================================================================
//  Main
// =====================================================================
int main() {
  fprintf(stderr, "===========================================\n");
  fprintf(stderr, "M8: Progression Hooks Tests\n");
  fprintf(stderr, "===========================================\n");

  test_threshold_linear();
  test_threshold_quadratic();
  test_threshold_from_thresholds();
  test_threshold_progress();
  test_progression_ids();
  test_enum_names();
  test_profile_basics();
  test_xp_no_levelup();
  test_xp_single_levelup();
  test_xp_multi_levelup();
  test_xp_max_level();
  test_xp_events();
  test_skill_points();
  test_custom_skill_points();
  test_set_level();
  test_flat_stat_growth();
  test_percentage_stat_growth();
  test_custom_stat_growth();
  test_stat_growth_on_levelup();
  test_skill_node_registration();
  test_skill_node_status();
  test_skill_unlock_success();
  test_skill_unlock_failures();
  test_skill_prerequisites();
  test_multi_rank_skills();
  test_skill_unlock_events();
  test_skill_stat_bonuses();
  test_skill_reset();
  test_skill_queries();
  test_growth_table_management();
  test_remove_profile();
  test_system_clear();
  test_clear_profiles();
  test_integrated_lifecycle();
  test_multiple_entities();
  test_stat_growth_reapplication();
  test_single_level_table();
  test_skill_granted_effect();
  test_growth_and_external_modifiers();
  test_event_entity_ids();

  fprintf(stderr, "===========================================\n");
  fprintf(stderr, "All 40 progression tests PASSED\n");
  fprintf(stderr, "===========================================\n");

  return 0;
}
