#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "zensim/gameplay/GameplayMechanicsLoader.hpp"

int main() {
  using namespace zs;

  // ====== MechanicsFieldValue factory methods and type accessors ======
  {
    auto sv = MechanicsFieldValue::make_string(SmallString{"hello"});
    assert(sv.type == MechanicsFieldType::string_field);
    assert(sv.stringValue == SmallString{"hello"});

    auto nv = MechanicsFieldValue::make_number(3.14);
    assert(nv.type == MechanicsFieldType::number_field);
    assert(nv.numberValue == 3.14);

    auto iv = MechanicsFieldValue::make_integer(42);
    assert(iv.type == MechanicsFieldType::integer_field);
    assert(iv.integerValue == 42);

    auto bv = MechanicsFieldValue::make_boolean(true);
    assert(bv.type == MechanicsFieldType::boolean_field);
    assert(bv.booleanValue == true);

    auto tv = MechanicsFieldValue::make_tag(GameplayTag{"ability.fire"});
    assert(tv.type == MechanicsFieldType::tag_field);
    assert(tv.tagValue == GameplayTag{"ability.fire"});

    auto rv = MechanicsFieldValue::make_reference(SmallString{"fireball"});
    assert(rv.type == MechanicsFieldType::reference_field);
    assert(rv.stringValue == SmallString{"fireball"});

    fprintf(stderr, "[PASS] MechanicsFieldValue factory methods\n");
  }

  // ====== MechanicsFieldType name helper ======
  {
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::string_field), "string") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::number_field), "number") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::integer_field), "integer") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::boolean_field), "boolean") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::tag_field), "tag") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::tag_list_field), "tag_list") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::reference_field), "reference") == 0);
    assert(std::strcmp(mechanics_field_type_name(MechanicsFieldType::number_list_field), "number_list") == 0);

    fprintf(stderr, "[PASS] mechanics_field_type_name\n");
  }

  // ====== MechanicsSchemaDescriptor ======
  {
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};
    schema.version = SmallString{"1.0"};

    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    schema.fields.push_back(costField);

    MechanicsFieldDescriptor cooldownField{};
    cooldownField.name = SmallString{"cooldown"};
    cooldownField.type = MechanicsFieldType::number_field;
    cooldownField.required = false;
    cooldownField.defaultValue = SmallString{};
    schema.fields.push_back(cooldownField);

    // find_field
    auto *found = schema.find_field(SmallString{"cost"});
    assert(found != nullptr);
    assert(found->name == SmallString{"cost"});
    assert(found->required == true);

    auto *found2 = schema.find_field(SmallString{"cooldown"});
    assert(found2 != nullptr);
    assert(found2->required == false);

    // Not found
    assert(schema.find_field(SmallString{"damage"}) == nullptr);

    fprintf(stderr, "[PASS] MechanicsSchemaDescriptor\n");
  }

  // ====== MechanicsDefinition ======
  {
    MechanicsDefinition def{};
    def.id = SmallString{"fireball"};
    def.category = SmallString{"ability"};
    def.tags.add(GameplayTag{"ability.fire"});

    // set_field / find_field
    def.set_field(SmallString{"cost"}, MechanicsFieldValue::make_number(25.0));
    def.set_field(SmallString{"damage"}, MechanicsFieldValue::make_number(50.0));
    def.set_field(SmallString{"name"}, MechanicsFieldValue::make_string(SmallString{"Fireball"}));
    def.set_field(SmallString{"active"}, MechanicsFieldValue::make_boolean(true));
    def.set_field(SmallString{"level"}, MechanicsFieldValue::make_integer(3));

    assert(def.find_field(SmallString{"cost"}) != nullptr);
    assert(def.find_field(SmallString{"cost"})->type == MechanicsFieldType::number_field);
    assert(def.find_field(SmallString{"nonexistent"}) == nullptr);

    // Typed getters with defaults
    assert(def.number_field(SmallString{"cost"}) == 25.0);
    assert(def.number_field(SmallString{"missing"}, 99.0) == 99.0);
    assert(def.string_field(SmallString{"name"}) == SmallString{"Fireball"});
    assert(def.string_field(SmallString{"missing"}, SmallString{"N/A"}) == SmallString{"N/A"});
    assert(def.boolean_field(SmallString{"active"}) == true);
    assert(def.boolean_field(SmallString{"missing"}, false) == false);
    assert(def.integer_field(SmallString{"level"}) == 3);
    assert(def.integer_field(SmallString{"missing"}, -1) == -1);

    // Overwrite existing field
    def.set_field(SmallString{"cost"}, MechanicsFieldValue::make_number(30.0));
    assert(def.number_field(SmallString{"cost"}) == 30.0);

    fprintf(stderr, "[PASS] MechanicsDefinition\n");
  }

  // ====== MechanicsRegistry ======
  {
    MechanicsRegistry registry{};
    assert(registry.schema_count() == 0);
    assert(registry.total_definition_count() == 0);

    // Create and register schema
    MechanicsSchemaDescriptor abilitySchema{};
    abilitySchema.category = SmallString{"ability"};
    abilitySchema.version = SmallString{"1.0"};

    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    abilitySchema.fields.push_back(costField);

    MechanicsFieldDescriptor dmgField{};
    dmgField.name = SmallString{"damage"};
    dmgField.type = MechanicsFieldType::number_field;
    dmgField.required = false;
    abilitySchema.fields.push_back(dmgField);

    assert(registry.register_schema(abilitySchema));
    assert(registry.schema_count() == 1);
    assert(registry.has_schema(SmallString{"ability"}));
    assert(!registry.has_schema(SmallString{"item"}));

    // Duplicate schema registration fails
    assert(!registry.register_schema(abilitySchema));
    assert(registry.schema_count() == 1);

    // find_schema
    auto *foundSchema = registry.find_schema(SmallString{"ability"});
    assert(foundSchema != nullptr);
    assert(foundSchema->category == SmallString{"ability"});
    assert(registry.find_schema(SmallString{"item"}) == nullptr);

    // Store definition
    MechanicsDefinition fireball{};
    fireball.id = SmallString{"fireball"};
    fireball.category = SmallString{"ability"};
    fireball.set_field(SmallString{"cost"}, MechanicsFieldValue::make_number(25.0));
    fireball.set_field(SmallString{"damage"}, MechanicsFieldValue::make_number(50.0));

    assert(registry.store_definition(fireball));
    assert(registry.total_definition_count() == 1);
    assert(registry.definition_count(SmallString{"ability"}) == 1);

    // Duplicate store fails
    assert(!registry.store_definition(fireball));
    assert(registry.total_definition_count() == 1);

    // Store to non-existent category fails
    MechanicsDefinition potion{};
    potion.id = SmallString{"health_potion"};
    potion.category = SmallString{"item"};
    assert(!registry.store_definition(potion));

    // find_definition
    auto *foundDef = registry.find_definition(SmallString{"ability"}, SmallString{"fireball"});
    assert(foundDef != nullptr);
    assert(foundDef->id == SmallString{"fireball"});
    assert(foundDef->number_field(SmallString{"cost"}) == 25.0);

    assert(registry.find_definition(SmallString{"ability"}, SmallString{"heal"}) == nullptr);
    assert(registry.find_definition(SmallString{"item"}, SmallString{"fireball"}) == nullptr);

    // has_definition
    assert(registry.has_definition(SmallString{"ability"}, SmallString{"fireball"}));
    assert(!registry.has_definition(SmallString{"ability"}, SmallString{"heal"}));

    // Store another definition
    MechanicsDefinition heal{};
    heal.id = SmallString{"heal"};
    heal.category = SmallString{"ability"};
    heal.set_field(SmallString{"cost"}, MechanicsFieldValue::make_number(15.0));
    assert(registry.store_definition(heal));
    assert(registry.definition_count(SmallString{"ability"}) == 2);
    assert(registry.total_definition_count() == 2);

    // definitions_in_category
    auto defs = registry.definitions_in_category(SmallString{"ability"});
    assert(defs.size() == 2);

    // Empty category
    auto emptyDefs = registry.definitions_in_category(SmallString{"item"});
    assert(emptyDefs.empty());

    // store_or_replace
    MechanicsDefinition fireballV2{};
    fireballV2.id = SmallString{"fireball"};
    fireballV2.category = SmallString{"ability"};
    fireballV2.set_field(SmallString{"cost"}, MechanicsFieldValue::make_number(30.0));
    assert(registry.store_or_replace_definition(fireballV2));
    auto *replaced = registry.find_definition(SmallString{"ability"}, SmallString{"fireball"});
    assert(replaced != nullptr);
    assert(replaced->number_field(SmallString{"cost"}) == 30.0);
    assert(registry.definition_count(SmallString{"ability"}) == 2); // count unchanged

    // remove_definition
    assert(registry.remove_definition(SmallString{"ability"}, SmallString{"heal"}));
    assert(registry.definition_count(SmallString{"ability"}) == 1);
    assert(!registry.remove_definition(SmallString{"ability"}, SmallString{"heal"}));

    // clear_category
    registry.store_definition(heal); // re-add
    assert(registry.definition_count(SmallString{"ability"}) == 2);
    size_t cleared = registry.clear_category(SmallString{"ability"});
    assert(cleared == 2);
    assert(registry.definition_count(SmallString{"ability"}) == 0);
    assert(registry.has_schema(SmallString{"ability"})); // schema preserved

    // clear_category on empty/non-existent
    assert(registry.clear_category(SmallString{"ability"}) == 0);
    assert(registry.clear_category(SmallString{"item"}) == 0);

    // remove_schema (also removes definitions)
    registry.store_definition(fireball);
    assert(registry.remove_schema(SmallString{"ability"}));
    assert(!registry.has_schema(SmallString{"ability"}));
    assert(registry.schema_count() == 0);
    assert(registry.total_definition_count() == 0);

    // remove_schema non-existent
    assert(!registry.remove_schema(SmallString{"ability"}));

    // clear all
    registry.register_schema(abilitySchema);
    registry.store_definition(fireball);
    registry.clear();
    assert(registry.schema_count() == 0);
    assert(registry.total_definition_count() == 0);

    fprintf(stderr, "[PASS] MechanicsRegistry\n");
  }

  // ====== JSON Loading: valid definitions ======
  {
    MechanicsRegistry registry{};

    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};
    schema.version = SmallString{"1.0"};

    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    schema.fields.push_back(costField);

    MechanicsFieldDescriptor dmgField{};
    dmgField.name = SmallString{"damage"};
    dmgField.type = MechanicsFieldType::number_field;
    dmgField.required = false;
    schema.fields.push_back(dmgField);

    MechanicsFieldDescriptor activeField{};
    activeField.name = SmallString{"active"};
    activeField.type = MechanicsFieldType::boolean_field;
    activeField.required = false;
    schema.fields.push_back(activeField);

    MechanicsFieldDescriptor levelField{};
    levelField.name = SmallString{"level"};
    levelField.type = MechanicsFieldType::integer_field;
    levelField.required = false;
    schema.fields.push_back(levelField);

    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability",
      "definitions": [
        {
          "id": "fireball",
          "tags": ["ability.fire", "ability.offensive"],
          "cost": 25.0,
          "damage": 50.0,
          "active": true,
          "level": 3
        },
        {
          "id": "heal",
          "tags": ["ability.holy"],
          "cost": 15.0,
          "damage": 0.0,
          "active": true,
          "level": 1
        }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(result.success);
    assert(result.definition_count() == 2);
    assert(result.error_count() == 0);

    // Verify first definition
    auto &fb = result.definitions[0];
    assert(fb.id == SmallString{"fireball"});
    assert(fb.category == SmallString{"ability"});
    assert(fb.number_field(SmallString{"cost"}) == 25.0);
    assert(fb.number_field(SmallString{"damage"}) == 50.0);
    assert(fb.boolean_field(SmallString{"active"}) == true);
    assert(fb.integer_field(SmallString{"level"}) == 3);
    assert(fb.tags.has(GameplayTag{"ability.fire"}));
    assert(fb.tags.has(GameplayTag{"ability.offensive"}));

    // Verify second definition
    auto &hl = result.definitions[1];
    assert(hl.id == SmallString{"heal"});
    assert(hl.number_field(SmallString{"cost"}) == 15.0);
    assert(hl.tags.has(GameplayTag{"ability.holy"}));

    fprintf(stderr, "[PASS] JSON loading: valid definitions\n");
  }

  // ====== JSON Loading: string_view overload ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"item"};
    MechanicsFieldDescriptor nameField{};
    nameField.name = SmallString{"name"};
    nameField.type = MechanicsFieldType::string_field;
    nameField.required = true;
    schema.fields.push_back(nameField);
    registry.register_schema(schema);

    std::string json = R"({
      "category": "item",
      "definitions": [
        { "id": "sword", "name": "Iron Sword" }
      ]
    })";

    auto result = load_mechanics_from_json(std::string_view{json}, registry);
    assert(result.success);
    assert(result.definition_count() == 1);
    assert(result.definitions[0].string_field(SmallString{"name"}) == SmallString{"Iron Sword"});

    fprintf(stderr, "[PASS] JSON loading: string_view overload\n");
  }

  // ====== JSON Loading: missing required field ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};

    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    schema.fields.push_back(costField);
    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "id": "fireball" }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.error_count() >= 1);
    assert(result.errors[0].definitionId == SmallString{"fireball"});
    assert(result.errors[0].fieldName == SmallString{"cost"});

    fprintf(stderr, "[PASS] JSON loading: missing required field\n");
  }

  // ====== JSON Loading: type mismatch ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};

    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    schema.fields.push_back(costField);
    registry.register_schema(schema);

    // cost is a string instead of a number
    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "id": "fireball", "cost": "not_a_number" }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.error_count() >= 1);
    assert(result.errors[0].fieldName == SmallString{"cost"});
    assert(result.errors[0].message == SmallString{"type mismatch"});

    fprintf(stderr, "[PASS] JSON loading: type mismatch\n");
  }

  // ====== JSON Loading: unknown category ======
  {
    MechanicsRegistry registry{};
    // No schemas registered

    std::string json = R"({
      "category": "ability",
      "definitions": []
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.error_count() >= 1);
    assert(result.errors[0].message == SmallString{"unknown category"});

    fprintf(stderr, "[PASS] JSON loading: unknown category\n");
  }

  // ====== JSON Loading: invalid JSON ======
  {
    MechanicsRegistry registry{};
    std::string json = "{ this is not valid json }}}";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.error_count() >= 1);
    assert(result.errors[0].message == SmallString{"JSON parse error"});

    fprintf(stderr, "[PASS] JSON loading: invalid JSON\n");
  }

  // ====== JSON Loading: missing category field ======
  {
    MechanicsRegistry registry{};
    std::string json = R"({
      "definitions": []
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.errors[0].message == SmallString{"missing category"});

    fprintf(stderr, "[PASS] JSON loading: missing category field\n");
  }

  // ====== JSON Loading: missing definitions array ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};
    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability"
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.errors[0].message == SmallString{"missing definitions"});

    fprintf(stderr, "[PASS] JSON loading: missing definitions array\n");
  }

  // ====== JSON Loading: missing id in definition ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};
    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "cost": 10 }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.errors[0].message == SmallString{"missing or invalid id"});

    fprintf(stderr, "[PASS] JSON loading: missing id\n");
  }

  // ====== JSON Loading: non-object definition ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};
    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability",
      "definitions": [ "not_an_object" ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(!result.success);
    assert(result.errors[0].message == SmallString{"not an object"});

    fprintf(stderr, "[PASS] JSON loading: non-object definition\n");
  }

  // ====== JSON Loading: default values ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};

    MechanicsFieldDescriptor descField{};
    descField.name = SmallString{"description"};
    descField.type = MechanicsFieldType::string_field;
    descField.required = false;
    descField.defaultValue = SmallString{"No description"};
    schema.fields.push_back(descField);

    MechanicsFieldDescriptor enabledField{};
    enabledField.name = SmallString{"enabled"};
    enabledField.type = MechanicsFieldType::boolean_field;
    enabledField.required = false;
    enabledField.defaultValue = SmallString{"true"};
    schema.fields.push_back(enabledField);

    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "id": "fireball" }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(result.success);
    assert(result.definition_count() == 1);

    auto &def = result.definitions[0];
    assert(def.string_field(SmallString{"description"}) == SmallString{"No description"});
    assert(def.boolean_field(SmallString{"enabled"}) == true);

    fprintf(stderr, "[PASS] JSON loading: default values\n");
  }

  // ====== JSON Loading: tag and tag_list fields ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"effect"};

    MechanicsFieldDescriptor tagField{};
    tagField.name = SmallString{"element"};
    tagField.type = MechanicsFieldType::tag_field;
    tagField.required = true;
    schema.fields.push_back(tagField);

    MechanicsFieldDescriptor tagListField{};
    tagListField.name = SmallString{"resistances"};
    tagListField.type = MechanicsFieldType::tag_list_field;
    tagListField.required = false;
    schema.fields.push_back(tagListField);

    MechanicsFieldDescriptor numListField{};
    numListField.name = SmallString{"multipliers"};
    numListField.type = MechanicsFieldType::number_list_field;
    numListField.required = false;
    schema.fields.push_back(numListField);

    registry.register_schema(schema);

    std::string json = R"({
      "category": "effect",
      "definitions": [
        {
          "id": "burn",
          "element": "element.fire",
          "resistances": ["element.water", "element.ice"],
          "multipliers": [1.0, 1.5, 2.0]
        }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(result.success);
    assert(result.definition_count() == 1);

    auto &def = result.definitions[0];
    auto *elemField = def.find_field(SmallString{"element"});
    assert(elemField != nullptr);
    assert(elemField->type == MechanicsFieldType::tag_field);
    assert(elemField->tagValue == GameplayTag{"element.fire"});

    auto *resField = def.find_field(SmallString{"resistances"});
    assert(resField != nullptr);
    assert(resField->type == MechanicsFieldType::tag_list_field);
    assert(resField->tagListValue.size() == 2);
    assert(resField->tagListValue[0] == GameplayTag{"element.water"});
    assert(resField->tagListValue[1] == GameplayTag{"element.ice"});

    auto *mulField = def.find_field(SmallString{"multipliers"});
    assert(mulField != nullptr);
    assert(mulField->type == MechanicsFieldType::number_list_field);
    assert(mulField->numberListValue.size() == 3);
    assert(mulField->numberListValue[0] == 1.0);
    assert(mulField->numberListValue[1] == 1.5);
    assert(mulField->numberListValue[2] == 2.0);

    fprintf(stderr, "[PASS] JSON loading: tag and list fields\n");
  }

  // ====== JSON Loading: reference fields ======
  {
    MechanicsRegistry registry{};

    MechanicsSchemaDescriptor effectSchema{};
    effectSchema.category = SmallString{"effect"};
    MechanicsFieldDescriptor effectNameField{};
    effectNameField.name = SmallString{"name"};
    effectNameField.type = MechanicsFieldType::string_field;
    effectNameField.required = true;
    effectSchema.fields.push_back(effectNameField);
    registry.register_schema(effectSchema);

    MechanicsSchemaDescriptor abilitySchema{};
    abilitySchema.category = SmallString{"ability"};
    MechanicsFieldDescriptor refField{};
    refField.name = SmallString{"effect_ref"};
    refField.type = MechanicsFieldType::reference_field;
    refField.required = false;
    refField.referenceCategory = SmallString{"effect"};
    abilitySchema.fields.push_back(refField);
    registry.register_schema(abilitySchema);

    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "id": "fireball", "effect_ref": "burn_effect" }
      ]
    })";

    auto result = load_mechanics_from_json(json.data(), json.size(), registry);
    assert(result.success);
    auto *refVal = result.definitions[0].find_field(SmallString{"effect_ref"});
    assert(refVal != nullptr);
    assert(refVal->type == MechanicsFieldType::reference_field);
    assert(refVal->stringValue == SmallString{"burn_effect"});

    fprintf(stderr, "[PASS] JSON loading: reference fields\n");
  }

  // ====== load_and_store_mechanics ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};

    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    schema.fields.push_back(costField);
    registry.register_schema(schema);

    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "id": "fireball", "cost": 25.0 },
        { "id": "heal", "cost": 15.0 }
      ]
    })";

    auto result = load_and_store_mechanics(std::string_view{json}, registry);
    assert(result.success);
    assert(result.definition_count() == 2);

    // Definitions should now be in the registry
    assert(registry.definition_count(SmallString{"ability"}) == 2);
    assert(registry.has_definition(SmallString{"ability"}, SmallString{"fireball"}));
    assert(registry.has_definition(SmallString{"ability"}, SmallString{"heal"}));

    auto *fb = registry.find_definition(SmallString{"ability"}, SmallString{"fireball"});
    assert(fb != nullptr);
    assert(fb->number_field(SmallString{"cost"}) == 25.0);

    fprintf(stderr, "[PASS] load_and_store_mechanics\n");
  }

  // ====== load_and_store_mechanics: failed load does not store ======
  {
    MechanicsRegistry registry{};
    MechanicsSchemaDescriptor schema{};
    schema.category = SmallString{"ability"};
    MechanicsFieldDescriptor costField{};
    costField.name = SmallString{"cost"};
    costField.type = MechanicsFieldType::number_field;
    costField.required = true;
    schema.fields.push_back(costField);
    registry.register_schema(schema);

    // Missing required field
    std::string json = R"({
      "category": "ability",
      "definitions": [
        { "id": "fireball" }
      ]
    })";

    auto result = load_and_store_mechanics(std::string_view{json}, registry);
    assert(!result.success);
    assert(registry.total_definition_count() == 0); // nothing stored

    fprintf(stderr, "[PASS] load_and_store_mechanics: failed load\n");
  }

  // ====== Content reference validation ======
  {
    MechanicsRegistry registry{};

    // Set up effect schema and definition
    MechanicsSchemaDescriptor effectSchema{};
    effectSchema.category = SmallString{"effect"};
    MechanicsFieldDescriptor nameField{};
    nameField.name = SmallString{"name"};
    nameField.type = MechanicsFieldType::string_field;
    nameField.required = true;
    effectSchema.fields.push_back(nameField);
    registry.register_schema(effectSchema);

    MechanicsDefinition burnEffect{};
    burnEffect.id = SmallString{"burn"};
    burnEffect.category = SmallString{"effect"};
    burnEffect.set_field(SmallString{"name"}, MechanicsFieldValue::make_string(SmallString{"Burn"}));
    registry.store_definition(burnEffect);

    // Set up ability schema with reference field
    MechanicsSchemaDescriptor abilitySchema{};
    abilitySchema.category = SmallString{"ability"};
    MechanicsFieldDescriptor refField{};
    refField.name = SmallString{"effect_ref"};
    refField.type = MechanicsFieldType::reference_field;
    refField.required = false;
    refField.referenceCategory = SmallString{"effect"};
    abilitySchema.fields.push_back(refField);
    registry.register_schema(abilitySchema);

    // Store ability with valid reference
    MechanicsDefinition fireball{};
    fireball.id = SmallString{"fireball"};
    fireball.category = SmallString{"ability"};
    fireball.set_field(SmallString{"effect_ref"},
                       MechanicsFieldValue::make_reference(SmallString{"burn"}));
    registry.store_definition(fireball);

    // Validate: should pass (burn exists in effect category)
    auto errors = registry.validate_references();
    assert(errors.empty());

    // Store ability with broken reference
    MechanicsDefinition iceBlast{};
    iceBlast.id = SmallString{"ice_blast"};
    iceBlast.category = SmallString{"ability"};
    iceBlast.set_field(SmallString{"effect_ref"},
                       MechanicsFieldValue::make_reference(SmallString{"freeze"}));
    registry.store_definition(iceBlast);

    // Validate: should detect the broken reference
    errors = registry.validate_references();
    assert(errors.size() == 1);
    assert(errors[0].definitionId == SmallString{"ice_blast"});
    assert(errors[0].fieldName == SmallString{"effect_ref"});
    assert(errors[0].message == SmallString{"broken ref"});

    // Fix by adding the missing effect
    MechanicsDefinition freezeEffect{};
    freezeEffect.id = SmallString{"freeze"};
    freezeEffect.category = SmallString{"effect"};
    freezeEffect.set_field(SmallString{"name"},
                           MechanicsFieldValue::make_string(SmallString{"Freeze"}));
    registry.store_definition(freezeEffect);

    errors = registry.validate_references();
    assert(errors.empty());

    fprintf(stderr, "[PASS] Content reference validation\n");
  }

  // ====== MechanicsLoadResult ======
  {
    MechanicsLoadResult result{};
    assert(result.success);
    assert(result.definition_count() == 0);
    assert(result.error_count() == 0);

    MechanicsDefinition def{};
    def.id = SmallString{"test"};
    result.definitions.push_back(def);
    assert(result.definition_count() == 1);

    MechanicsLoadError err{};
    err.message = SmallString{"test error"};
    result.errors.push_back(err);
    assert(result.error_count() == 1);

    fprintf(stderr, "[PASS] MechanicsLoadResult\n");
  }

  fprintf(stderr, "[ALL PASS] gameplay_mechanics_loading\n");
  return 0;
}
