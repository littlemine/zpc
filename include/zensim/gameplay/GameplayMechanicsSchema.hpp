#pragma once

#include <string>
#include <vector>

#include "zensim/TypeAlias.hpp"
#include "zensim/gameplay/GameplayTag.hpp"

namespace zs {

  /// Supported field types in a mechanics definition schema.
  enum class MechanicsFieldType : u8 {
    string_field,     ///< SmallString value
    number_field,     ///< double-precision floating point
    integer_field,    ///< signed 64-bit integer
    boolean_field,    ///< true/false
    tag_field,        ///< GameplayTag (dot-separated hierarchy)
    tag_list_field,   ///< vector of GameplayTag values
    reference_field,  ///< reference to another definition by id
    number_list_field ///< vector of double values
  };

  /// Returns a human-readable name for a field type.
  inline const char *mechanics_field_type_name(MechanicsFieldType type) noexcept {
    switch (type) {
      case MechanicsFieldType::string_field: return "string";
      case MechanicsFieldType::number_field: return "number";
      case MechanicsFieldType::integer_field: return "integer";
      case MechanicsFieldType::boolean_field: return "boolean";
      case MechanicsFieldType::tag_field: return "tag";
      case MechanicsFieldType::tag_list_field: return "tag_list";
      case MechanicsFieldType::reference_field: return "reference";
      case MechanicsFieldType::number_list_field: return "number_list";
      default: return "unknown";
    }
  }

  /// Describes a single field in a mechanics schema.
  ///
  /// Field descriptors define the shape of a mechanics definition category
  /// (e.g., what fields an "ability" definition must or may have).
  struct MechanicsFieldDescriptor {
    SmallString name{};            ///< Field name used as the JSON key
    MechanicsFieldType type{MechanicsFieldType::string_field};
    bool required{false};          ///< Whether the field must be present
    SmallString defaultValue{};    ///< Default value serialized as string
    SmallString referenceCategory{}; ///< For reference_field: which category the id refers to
  };

  /// Describes the schema for a category of mechanics definitions.
  ///
  /// A schema defines the expected structure for definitions of a given
  /// category (e.g., "ability", "effect", "item"). The loader validates
  /// incoming JSON against this schema and reports errors for missing
  /// required fields or type mismatches.
  struct MechanicsSchemaDescriptor {
    SmallString category{};  ///< Category name (e.g., "ability")
    SmallString version{};   ///< Schema version for migration support

    std::vector<MechanicsFieldDescriptor> fields{};

    /// Find a field descriptor by name. Returns nullptr if not found.
    const MechanicsFieldDescriptor *find_field(const SmallString &fieldName) const noexcept {
      for (const auto &field : fields) {
        if (field.name == fieldName) return &field;
      }
      return nullptr;
    }
  };

  /// A single typed value in a mechanics definition.
  struct MechanicsFieldValue {
    MechanicsFieldType type{MechanicsFieldType::string_field};

    SmallString stringValue{};
    double numberValue{0.0};
    i64 integerValue{0};
    bool booleanValue{false};
    GameplayTag tagValue{};
    std::vector<GameplayTag> tagListValue{};
    std::vector<double> numberListValue{};

    /// Convenience constructors for each type.
    static MechanicsFieldValue make_string(const SmallString &v) {
      MechanicsFieldValue fv{};
      fv.type = MechanicsFieldType::string_field;
      fv.stringValue = v;
      return fv;
    }

    static MechanicsFieldValue make_number(double v) {
      MechanicsFieldValue fv{};
      fv.type = MechanicsFieldType::number_field;
      fv.numberValue = v;
      return fv;
    }

    static MechanicsFieldValue make_integer(i64 v) {
      MechanicsFieldValue fv{};
      fv.type = MechanicsFieldType::integer_field;
      fv.integerValue = v;
      return fv;
    }

    static MechanicsFieldValue make_boolean(bool v) {
      MechanicsFieldValue fv{};
      fv.type = MechanicsFieldType::boolean_field;
      fv.booleanValue = v;
      return fv;
    }

    static MechanicsFieldValue make_tag(const GameplayTag &v) {
      MechanicsFieldValue fv{};
      fv.type = MechanicsFieldType::tag_field;
      fv.tagValue = v;
      return fv;
    }

    static MechanicsFieldValue make_reference(const SmallString &refId) {
      MechanicsFieldValue fv{};
      fv.type = MechanicsFieldType::reference_field;
      fv.stringValue = refId;
      return fv;
    }
  };

  /// A loaded and validated mechanics definition.
  ///
  /// Definitions are the runtime representation of data-driven gameplay
  /// entries (abilities, effects, items, etc.). Each definition belongs to
  /// a category, has a unique id within that category, and contains a set
  /// of typed field values conforming to the category's schema.
  struct MechanicsDefinition {
    SmallString id{};         ///< Unique identifier within its category
    SmallString category{};   ///< Which schema this definition follows
    GameplayTagContainer tags{};  ///< Optional tags for classification/queries

    std::vector<std::pair<SmallString, MechanicsFieldValue>> fields{};

    /// Look up a field by name. Returns nullptr if not found.
    const MechanicsFieldValue *find_field(const SmallString &name) const noexcept {
      for (const auto &entry : fields) {
        if (entry.first == name) return &entry.second;
      }
      return nullptr;
    }

    /// Set or overwrite a field value.
    void set_field(const SmallString &name, MechanicsFieldValue value) {
      for (auto &entry : fields) {
        if (entry.first == name) {
          entry.second = static_cast<MechanicsFieldValue &&>(value);
          return;
        }
      }
      fields.push_back({name, static_cast<MechanicsFieldValue &&>(value)});
    }

    /// Get a string field value with a default.
    SmallString string_field(const SmallString &name,
                             const SmallString &defaultVal = {}) const noexcept {
      auto *fv = find_field(name);
      if (fv && fv->type == MechanicsFieldType::string_field) return fv->stringValue;
      return defaultVal;
    }

    /// Get a number field value with a default.
    double number_field(const SmallString &name, double defaultVal = 0.0) const noexcept {
      auto *fv = find_field(name);
      if (fv && fv->type == MechanicsFieldType::number_field) return fv->numberValue;
      return defaultVal;
    }

    /// Get an integer field value with a default.
    i64 integer_field(const SmallString &name, i64 defaultVal = 0) const noexcept {
      auto *fv = find_field(name);
      if (fv && fv->type == MechanicsFieldType::integer_field) return fv->integerValue;
      return defaultVal;
    }

    /// Get a boolean field value with a default.
    bool boolean_field(const SmallString &name, bool defaultVal = false) const noexcept {
      auto *fv = find_field(name);
      if (fv && fv->type == MechanicsFieldType::boolean_field) return fv->booleanValue;
      return defaultVal;
    }
  };

  /// Describes a single error encountered during mechanics loading.
  struct MechanicsLoadError {
    SmallString definitionId{};  ///< Which definition had the problem (empty if global)
    SmallString fieldName{};     ///< Which field had the problem (empty if global)
    SmallString message{};       ///< Human-readable error description
  };

  /// Result of a mechanics loading operation.
  struct MechanicsLoadResult {
    bool success{true};
    std::vector<MechanicsDefinition> definitions{};
    std::vector<MechanicsLoadError> errors{};

    size_t definition_count() const noexcept { return definitions.size(); }
    size_t error_count() const noexcept { return errors.size(); }
  };

}  // namespace zs
