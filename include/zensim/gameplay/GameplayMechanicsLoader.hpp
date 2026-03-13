#pragma once

#include <string>

#include "zensim/gameplay/GameplayMechanicsRegistry.hpp"
#include "zensim/zpc_tpls/rapidjson/document.h"

namespace zs {

  /// Parse a mechanics field value from a rapidjson Value according to
  /// the expected field type. Returns true on success.
  inline bool parse_mechanics_field_value(const rapidjson::Value &jsonValue,
                                          MechanicsFieldType expectedType,
                                          MechanicsFieldValue &out) {
    switch (expectedType) {
      case MechanicsFieldType::string_field:
        if (!jsonValue.IsString()) return false;
        out = MechanicsFieldValue::make_string(SmallString{jsonValue.GetString()});
        return true;

      case MechanicsFieldType::number_field:
        if (!jsonValue.IsNumber()) return false;
        out = MechanicsFieldValue::make_number(jsonValue.GetDouble());
        return true;

      case MechanicsFieldType::integer_field:
        if (jsonValue.IsInt64()) {
          out = MechanicsFieldValue::make_integer(jsonValue.GetInt64());
          return true;
        }
        if (jsonValue.IsInt()) {
          out = MechanicsFieldValue::make_integer(static_cast<i64>(jsonValue.GetInt()));
          return true;
        }
        return false;

      case MechanicsFieldType::boolean_field:
        if (!jsonValue.IsBool()) return false;
        out = MechanicsFieldValue::make_boolean(jsonValue.GetBool());
        return true;

      case MechanicsFieldType::tag_field:
        if (!jsonValue.IsString()) return false;
        out = MechanicsFieldValue::make_tag(GameplayTag{jsonValue.GetString()});
        return true;

      case MechanicsFieldType::reference_field:
        if (!jsonValue.IsString()) return false;
        out = MechanicsFieldValue::make_reference(SmallString{jsonValue.GetString()});
        return true;

      case MechanicsFieldType::tag_list_field: {
        if (!jsonValue.IsArray()) return false;
        MechanicsFieldValue fv{};
        fv.type = MechanicsFieldType::tag_list_field;
        for (rapidjson::SizeType i = 0; i < jsonValue.Size(); ++i) {
          if (!jsonValue[i].IsString()) return false;
          fv.tagListValue.push_back(GameplayTag{jsonValue[i].GetString()});
        }
        out = static_cast<MechanicsFieldValue &&>(fv);
        return true;
      }

      case MechanicsFieldType::number_list_field: {
        if (!jsonValue.IsArray()) return false;
        MechanicsFieldValue fv{};
        fv.type = MechanicsFieldType::number_list_field;
        for (rapidjson::SizeType i = 0; i < jsonValue.Size(); ++i) {
          if (!jsonValue[i].IsNumber()) return false;
          fv.numberListValue.push_back(jsonValue[i].GetDouble());
        }
        out = static_cast<MechanicsFieldValue &&>(fv);
        return true;
      }

      default:
        return false;
    }
  }

  /// Load a single mechanics definition from a rapidjson object, validated
  /// against the given schema.
  ///
  /// Expected JSON shape:
  /// ```json
  /// {
  ///   "id": "fireball",
  ///   "tags": ["ability.fire", "ability.offensive"],
  ///   "cost": 25.0,
  ///   "cooldown": 3.0,
  ///   "damage": 50.0
  /// }
  /// ```
  inline MechanicsLoadResult load_single_definition(
      const rapidjson::Value &obj,
      const MechanicsSchemaDescriptor &schema) {
    MechanicsLoadResult result{};

    if (!obj.IsObject()) {
      MechanicsLoadError err{};
      err.message = SmallString{"not an object"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }

    MechanicsDefinition def{};
    def.category = schema.category;

    // Read id (required)
    auto idIt = obj.FindMember("id");
    if (idIt == obj.MemberEnd() || !idIt->value.IsString()) {
      MechanicsLoadError err{};
      err.fieldName = SmallString{"id"};
      err.message = SmallString{"missing or invalid id"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }
    def.id = SmallString{idIt->value.GetString()};

    // Read optional tags array
    auto tagsIt = obj.FindMember("tags");
    if (tagsIt != obj.MemberEnd() && tagsIt->value.IsArray()) {
      for (rapidjson::SizeType i = 0; i < tagsIt->value.Size(); ++i) {
        if (tagsIt->value[i].IsString()) {
          def.tags.add(GameplayTag{tagsIt->value[i].GetString()});
        }
      }
    }

    // Read fields according to schema
    for (const auto &fieldDesc : schema.fields) {
      auto fieldIt = obj.FindMember(fieldDesc.name.asChars());
      if (fieldIt == obj.MemberEnd()) {
        if (fieldDesc.required) {
          MechanicsLoadError err{};
          err.definitionId = def.id;
          err.fieldName = fieldDesc.name;
          err.message = SmallString{"required field missing"};
          result.errors.push_back(err);
          result.success = false;
        }
        // Apply default if available
        if (fieldDesc.defaultValue.size() > 0) {
          // Simple defaults: parse string as the appropriate type
          MechanicsFieldValue fv{};
          switch (fieldDesc.type) {
            case MechanicsFieldType::string_field:
              fv = MechanicsFieldValue::make_string(fieldDesc.defaultValue);
              break;
            case MechanicsFieldType::boolean_field:
              fv = MechanicsFieldValue::make_boolean(
                  fieldDesc.defaultValue == SmallString{"true"});
              break;
            default:
              // For other types, default parsing from string is not
              // supported — skip.
              continue;
          }
          def.set_field(fieldDesc.name, static_cast<MechanicsFieldValue &&>(fv));
        }
        continue;
      }

      MechanicsFieldValue fv{};
      if (!parse_mechanics_field_value(fieldIt->value, fieldDesc.type, fv)) {
        MechanicsLoadError err{};
        err.definitionId = def.id;
        err.fieldName = fieldDesc.name;
        err.message = SmallString{"type mismatch"};
        result.errors.push_back(err);
        result.success = false;
        continue;
      }
      def.set_field(fieldDesc.name, static_cast<MechanicsFieldValue &&>(fv));
    }

    if (result.success) {
      result.definitions.push_back(static_cast<MechanicsDefinition &&>(def));
    }
    return result;
  }

  /// Load mechanics definitions from a JSON string.
  ///
  /// Expected JSON format:
  /// ```json
  /// {
  ///   "category": "ability",
  ///   "definitions": [
  ///     { "id": "fireball", "cost": 25.0, ... },
  ///     { "id": "heal",     "cost": 15.0, ... }
  ///   ]
  /// }
  /// ```
  ///
  /// The category in the JSON must match a registered schema. Definitions
  /// are validated against the schema and errors are collected.
  inline MechanicsLoadResult load_mechanics_from_json(
      const char *json, size_t length,
      const MechanicsRegistry &registry) {
    MechanicsLoadResult result{};

    rapidjson::Document document;
    document.Parse(json, length);

    if (document.HasParseError()) {
      MechanicsLoadError err{};
      err.message = SmallString{"JSON parse error"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }

    if (!document.IsObject()) {
      MechanicsLoadError err{};
      err.message = SmallString{"root not an object"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }

    // Read category
    auto catIt = document.FindMember("category");
    if (catIt == document.MemberEnd() || !catIt->value.IsString()) {
      MechanicsLoadError err{};
      err.message = SmallString{"missing category"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }
    SmallString category{catIt->value.GetString()};

    // Find schema
    const auto *schema = registry.find_schema(category);
    if (!schema) {
      MechanicsLoadError err{};
      err.message = SmallString{"unknown category"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }

    // Read definitions array
    auto defsIt = document.FindMember("definitions");
    if (defsIt == document.MemberEnd() || !defsIt->value.IsArray()) {
      MechanicsLoadError err{};
      err.message = SmallString{"missing definitions"};
      result.errors.push_back(err);
      result.success = false;
      return result;
    }

    for (rapidjson::SizeType i = 0; i < defsIt->value.Size(); ++i) {
      auto defResult = load_single_definition(defsIt->value[i], *schema);
      for (auto &def : defResult.definitions) {
        result.definitions.push_back(static_cast<MechanicsDefinition &&>(def));
      }
      for (auto &err : defResult.errors) {
        result.errors.push_back(static_cast<MechanicsLoadError &&>(err));
      }
      if (!defResult.success) {
        result.success = false;
      }
    }

    return result;
  }

  /// Convenience overload taking a std::string_view.
  inline MechanicsLoadResult load_mechanics_from_json(
      std::string_view json,
      const MechanicsRegistry &registry) {
    return load_mechanics_from_json(json.data(), json.size(), registry);
  }

  /// Load definitions from JSON and store them directly into a registry.
  /// Returns the load result (check success and errors).
  inline MechanicsLoadResult load_and_store_mechanics(
      std::string_view json,
      MechanicsRegistry &registry) {
    auto result = load_mechanics_from_json(json, registry);
    if (result.success) {
      for (const auto &def : result.definitions) {
        registry.store_definition(def);
      }
    }
    return result;
  }

}  // namespace zs
