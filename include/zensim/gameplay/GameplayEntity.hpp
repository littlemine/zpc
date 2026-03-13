#pragma once

#include <unordered_map>
#include <vector>

#include "zensim/TypeAlias.hpp"
#include "zensim/gameplay/GameplayTag.hpp"

namespace zs {

  /// Unique identifier for a gameplay entity. Wraps a u64 for type safety.
  struct GameplayEntityId {
    u64 value{0};

    constexpr GameplayEntityId() noexcept = default;
    constexpr explicit GameplayEntityId(u64 v) noexcept : value{v} {}

    bool valid() const noexcept { return value != 0; }
    bool operator==(const GameplayEntityId &other) const noexcept { return value == other.value; }
    bool operator!=(const GameplayEntityId &other) const noexcept { return value != other.value; }
  };

  struct GameplayEntityIdHash {
    size_t operator()(const GameplayEntityId &id) const noexcept {
      return static_cast<size_t>(id.value);
    }
  };

  /// Type identifier for components. Each component type gets a unique u32.
  struct GameplayComponentTypeId {
    u32 value{0};

    constexpr GameplayComponentTypeId() noexcept = default;
    constexpr explicit GameplayComponentTypeId(u32 v) noexcept : value{v} {}

    bool operator==(const GameplayComponentTypeId &other) const noexcept {
      return value == other.value;
    }
    bool operator!=(const GameplayComponentTypeId &other) const noexcept {
      return value != other.value;
    }
  };

  struct GameplayComponentTypeIdHash {
    size_t operator()(const GameplayComponentTypeId &id) const noexcept {
      return static_cast<size_t>(id.value);
    }
  };

  /// Base class for all gameplay components. Components are pure data holders
  /// identified by their type id.
  struct GameplayComponent {
    virtual ~GameplayComponent() = default;
    virtual GameplayComponentTypeId type_id() const noexcept = 0;
    virtual const char *type_name() const noexcept = 0;
  };

  /// A gameplay entity is a runtime object with an identity, a tag container,
  /// and an extensible set of components. Entities are the primary unit of
  /// gameplay state.
  ///
  /// Design notes:
  /// - Entities own their components via unique ownership (unique_ptr-like
  ///   semantics using raw pointers with manual lifecycle management here for
  ///   ZPC compatibility).
  /// - Entity identity is assigned by the GameplayWorld and is unique within
  ///   a world instance.
  /// - Tags are stored directly on the entity for fast per-entity queries.
  class GameplayEntity {
  public:
    GameplayEntity() noexcept = default;
    explicit GameplayEntity(GameplayEntityId id) noexcept : _id{id} {}

    GameplayEntity(GameplayEntity &&other) noexcept
        : _id{other._id},
          _label{other._label},
          _tags{static_cast<GameplayTagContainer &&>(other._tags)},
          _components{static_cast<std::unordered_map<GameplayComponentTypeId,
                                                     GameplayComponent *,
                                                     GameplayComponentTypeIdHash> &&>(
              other._components)},
          _alive{other._alive} {
      other._components.clear();
      other._alive = false;
    }

    GameplayEntity &operator=(GameplayEntity &&other) noexcept {
      if (this != &other) {
        destroy_all_components();
        _id = other._id;
        _label = other._label;
        _tags = static_cast<GameplayTagContainer &&>(other._tags);
        _components = static_cast<std::unordered_map<GameplayComponentTypeId,
                                                     GameplayComponent *,
                                                     GameplayComponentTypeIdHash> &&>(
            other._components);
        _alive = other._alive;
        other._components.clear();
        other._alive = false;
      }
      return *this;
    }

    // No copy — entities have unique identity.
    GameplayEntity(const GameplayEntity &) = delete;
    GameplayEntity &operator=(const GameplayEntity &) = delete;

    ~GameplayEntity() { destroy_all_components(); }

    GameplayEntityId id() const noexcept { return _id; }
    const SmallString &label() const noexcept { return _label; }
    void set_label(const SmallString &label) { _label = label; }

    bool alive() const noexcept { return _alive; }
    void mark_dead() noexcept { _alive = false; }

    // ---- Tag access ----
    GameplayTagContainer &tags() noexcept { return _tags; }
    const GameplayTagContainer &tags() const noexcept { return _tags; }

    // ---- Component access ----

    /// Attach a component. Takes ownership. Returns false if a component of the
    /// same type is already attached.
    bool attach_component(GameplayComponent *component) {
      if (!component) return false;
      const auto typeId = component->type_id();
      auto [it, inserted] = _components.emplace(typeId, component);
      return inserted;
    }

    /// Detach and return a component by type id. Caller takes ownership.
    /// Returns nullptr if not found.
    GameplayComponent *detach_component(GameplayComponentTypeId typeId) {
      auto it = _components.find(typeId);
      if (it == _components.end()) return nullptr;
      auto *comp = it->second;
      _components.erase(it);
      return comp;
    }

    /// Get a component by type id. Returns nullptr if not found.
    GameplayComponent *component(GameplayComponentTypeId typeId) noexcept {
      auto it = _components.find(typeId);
      return it != _components.end() ? it->second : nullptr;
    }

    const GameplayComponent *component(GameplayComponentTypeId typeId) const noexcept {
      auto it = _components.find(typeId);
      return it != _components.end() ? it->second : nullptr;
    }

    /// Check whether a component of the given type is attached.
    bool has_component(GameplayComponentTypeId typeId) const noexcept {
      return _components.find(typeId) != _components.end();
    }

    size_t component_count() const noexcept { return _components.size(); }

    /// Visit all attached components.
    template <typename Fn> void for_each_component(Fn &&fn) {
      for (auto &entry : _components) fn(entry.first, *entry.second);
    }

    template <typename Fn> void for_each_component(Fn &&fn) const {
      for (const auto &entry : _components) fn(entry.first, *entry.second);
    }

  private:
    void destroy_all_components() {
      for (auto &[typeId, comp] : _components) delete comp;
      _components.clear();
    }

    GameplayEntityId _id{};
    SmallString _label{};
    GameplayTagContainer _tags{};
    std::unordered_map<GameplayComponentTypeId, GameplayComponent *, GameplayComponentTypeIdHash>
        _components{};
    bool _alive{true};
  };

  /// The gameplay world manages entity lifecycle: creation, lookup, iteration,
  /// and destruction. It is the single authority for entity identity within a
  /// gameplay session.
  class GameplayWorld {
  public:
    GameplayWorld() = default;

    /// Create a new entity with a unique id.
    GameplayEntityId create_entity() {
      GameplayEntityId id{++_nextId};
      _entities.emplace(id, GameplayEntity{id});
      return id;
    }

    /// Create a new entity with a label.
    GameplayEntityId create_entity(const SmallString &label) {
      GameplayEntityId id{++_nextId};
      auto &entity = _entities.emplace(id, GameplayEntity{id}).first->second;
      entity.set_label(label);
      return id;
    }

    /// Destroy an entity. Returns true if the entity existed.
    bool destroy_entity(GameplayEntityId id) {
      auto it = _entities.find(id);
      if (it == _entities.end()) return false;
      it->second.mark_dead();
      _entities.erase(it);
      return true;
    }

    /// Look up an entity by id. Returns nullptr if not found.
    GameplayEntity *entity(GameplayEntityId id) noexcept {
      auto it = _entities.find(id);
      return it != _entities.end() ? &it->second : nullptr;
    }

    const GameplayEntity *entity(GameplayEntityId id) const noexcept {
      auto it = _entities.find(id);
      return it != _entities.end() ? &it->second : nullptr;
    }

    /// Check whether an entity exists.
    bool exists(GameplayEntityId id) const noexcept {
      return _entities.find(id) != _entities.end();
    }

    size_t entity_count() const noexcept { return _entities.size(); }

    /// Visit all living entities.
    template <typename Fn> void for_each_entity(Fn &&fn) {
      for (auto &[id, entity] : _entities) fn(id, entity);
    }

    template <typename Fn> void for_each_entity(Fn &&fn) const {
      for (const auto &[id, entity] : _entities) fn(id, entity);
    }

    /// Collect all entity ids that have a specific tag.
    std::vector<GameplayEntityId> entities_with_tag(const GameplayTag &tag) const {
      std::vector<GameplayEntityId> result{};
      for (const auto &[id, entity] : _entities) {
        if (entity.tags().has(tag)) result.push_back(id);
      }
      return result;
    }

    /// Collect all entity ids that have any child of the given parent tag.
    std::vector<GameplayEntityId> entities_with_tag_child_of(const GameplayTag &parent) const {
      std::vector<GameplayEntityId> result{};
      for (const auto &[id, entity] : _entities) {
        if (entity.tags().has_any_child_of(parent)) result.push_back(id);
      }
      return result;
    }

  private:
    std::unordered_map<GameplayEntityId, GameplayEntity, GameplayEntityIdHash> _entities{};
    u64 _nextId{0};
  };

}  // namespace zs
