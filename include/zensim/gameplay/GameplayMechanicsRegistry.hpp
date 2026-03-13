#pragma once

#include <unordered_map>
#include <vector>

#include "zensim/gameplay/GameplayMechanicsSchema.hpp"

namespace zs {

  /// Runtime registry for mechanics schemas and definitions.
  ///
  /// The registry is the central store for all loaded mechanics data.
  /// Schemas are registered first, then definitions are loaded and stored.
  /// Definitions can be looked up by category and id, iterated by category,
  /// and removed for hot-reload.
  ///
  /// Usage pattern:
  ///   1. Register schemas for each category (ability, effect, item, etc.)
  ///   2. Load definitions from JSON using the loader
  ///   3. Query definitions at runtime for gameplay logic
  ///   4. For hot-reload: clear a category and re-load
  class MechanicsRegistry {
  public:
    MechanicsRegistry() = default;

    // ---- Schema management ----

    /// Register a schema for a category. Returns false if the category
    /// already has a schema registered.
    bool register_schema(const MechanicsSchemaDescriptor &schema) {
      auto key = std::string(schema.category.asChars());
      auto [it, inserted] = _schemas.emplace(key, schema);
      return inserted;
    }

    /// Find the schema for a category. Returns nullptr if not registered.
    const MechanicsSchemaDescriptor *find_schema(const SmallString &category) const noexcept {
      auto it = _schemas.find(std::string(category.asChars()));
      return it != _schemas.end() ? &it->second : nullptr;
    }

    /// Check whether a schema exists for the given category.
    bool has_schema(const SmallString &category) const noexcept {
      return _schemas.find(std::string(category.asChars())) != _schemas.end();
    }

    /// Remove a schema. Also removes all definitions in that category.
    /// Returns true if the schema existed.
    bool remove_schema(const SmallString &category) {
      auto key = std::string(category.asChars());
      if (_schemas.erase(key) == 0) return false;
      _definitions.erase(key);
      return true;
    }

    size_t schema_count() const noexcept { return _schemas.size(); }

    // ---- Definition management ----

    /// Store a definition. The definition's category must have a registered
    /// schema. Returns false if the definition's id already exists in its
    /// category, or if no schema is registered for the category.
    bool store_definition(const MechanicsDefinition &def) {
      auto catKey = std::string(def.category.asChars());
      if (_schemas.find(catKey) == _schemas.end()) return false;

      auto &defs = _definitions[catKey];
      auto idKey = std::string(def.id.asChars());
      auto [it, inserted] = defs.emplace(idKey, def);
      return inserted;
    }

    /// Store a definition, overwriting any existing definition with the
    /// same category and id. Returns false if no schema is registered.
    bool store_or_replace_definition(const MechanicsDefinition &def) {
      auto catKey = std::string(def.category.asChars());
      if (_schemas.find(catKey) == _schemas.end()) return false;

      auto &defs = _definitions[catKey];
      auto idKey = std::string(def.id.asChars());
      defs[idKey] = def;
      return true;
    }

    /// Find a definition by category and id. Returns nullptr if not found.
    const MechanicsDefinition *find_definition(const SmallString &category,
                                                const SmallString &id) const noexcept {
      auto catIt = _definitions.find(std::string(category.asChars()));
      if (catIt == _definitions.end()) return nullptr;
      auto idIt = catIt->second.find(std::string(id.asChars()));
      return idIt != catIt->second.end() ? &idIt->second : nullptr;
    }

    /// Check whether a definition exists.
    bool has_definition(const SmallString &category, const SmallString &id) const noexcept {
      return find_definition(category, id) != nullptr;
    }

    /// Remove a definition. Returns true if found and removed.
    bool remove_definition(const SmallString &category, const SmallString &id) {
      auto catIt = _definitions.find(std::string(category.asChars()));
      if (catIt == _definitions.end()) return false;
      return catIt->second.erase(std::string(id.asChars())) > 0;
    }

    /// Get all definitions in a category.
    std::vector<const MechanicsDefinition *> definitions_in_category(
        const SmallString &category) const {
      std::vector<const MechanicsDefinition *> result{};
      auto catIt = _definitions.find(std::string(category.asChars()));
      if (catIt == _definitions.end()) return result;
      result.reserve(catIt->second.size());
      for (const auto &entry : catIt->second) {
        result.push_back(&entry.second);
      }
      return result;
    }

    /// Get the count of definitions in a category.
    size_t definition_count(const SmallString &category) const noexcept {
      auto catIt = _definitions.find(std::string(category.asChars()));
      return catIt != _definitions.end() ? catIt->second.size() : 0;
    }

    /// Get the total count of definitions across all categories.
    size_t total_definition_count() const noexcept {
      size_t total = 0;
      for (const auto &entry : _definitions) total += entry.second.size();
      return total;
    }

    /// Clear all definitions in a category (for hot-reload). Schema is kept.
    /// Returns the number of definitions removed.
    size_t clear_category(const SmallString &category) {
      auto catIt = _definitions.find(std::string(category.asChars()));
      if (catIt == _definitions.end()) return 0;
      size_t count = catIt->second.size();
      catIt->second.clear();
      return count;
    }

    /// Clear everything: all schemas and definitions.
    void clear() {
      _schemas.clear();
      _definitions.clear();
    }

    // ---- Content reference validation ----

    /// Validate all cross-references between definitions. A reference_field
    /// value should point to an existing definition in the referenced category.
    /// Returns a list of errors for broken references.
    std::vector<MechanicsLoadError> validate_references() const {
      std::vector<MechanicsLoadError> errors{};

      for (const auto &catEntry : _definitions) {
        auto *schema = find_schema(SmallString{catEntry.first.c_str()});
        if (!schema) continue;

        for (const auto &defEntry : catEntry.second) {
          const auto &def = defEntry.second;
          for (const auto &field : def.fields) {
            auto *fieldDesc = schema->find_field(field.first);
            if (!fieldDesc) continue;
            if (fieldDesc->type != MechanicsFieldType::reference_field) continue;

            // Check that the referenced definition exists
            const auto &refId = field.second.stringValue;
            const auto &refCategory = fieldDesc->referenceCategory;
            if (refId.size() > 0 && !has_definition(refCategory, refId)) {
              MechanicsLoadError err{};
              err.definitionId = def.id;
              err.fieldName = field.first;
              err.message = SmallString{"broken ref"};
              errors.push_back(err);
            }
          }
        }
      }
      return errors;
    }

  private:
    std::unordered_map<std::string, MechanicsSchemaDescriptor> _schemas{};
    std::unordered_map<std::string,
                       std::unordered_map<std::string, MechanicsDefinition>> _definitions{};
  };

}  // namespace zs
