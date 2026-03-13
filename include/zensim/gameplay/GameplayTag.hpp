#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "zensim/types/SmallVector.hpp"

namespace zs {

  /// A gameplay tag is a lightweight hierarchical identifier used for state,
  /// conditions, and classification throughout the gameplay mechanics system.
  ///
  /// Tags use dot-separated hierarchical names such as "status.burning" or
  /// "ability.cooldown.active". Hierarchy allows queries like "does this
  /// container have any tag under status.*?"
  ///
  /// Tags are value types based on SmallString and can be compared, hashed,
  /// serialized, and stored in containers efficiently.
  struct GameplayTag {
    SmallString name{};

    constexpr GameplayTag() noexcept = default;
    GameplayTag(const char *tagName) : name{tagName} {}
    GameplayTag(const SmallString &tagName) : name{tagName} {}

    bool operator==(const GameplayTag &other) const noexcept { return name == other.name; }
    bool operator!=(const GameplayTag &other) const noexcept { return !(name == other.name); }
    bool operator<(const GameplayTag &other) const noexcept {
      return std::strcmp(name.asChars(), other.name.asChars()) < 0;
    }

    /// Check whether this tag is a child of (or equal to) the given parent tag.
    /// For example, "status.burning.strong" is_child_of "status.burning" and
    /// "status".
    bool is_child_of(const GameplayTag &parent) const noexcept {
      const auto parentLen = parent.name.size();
      if (parentLen == 0) return true;
      if (name.size() < parentLen) return false;
      if (std::memcmp(name.asChars(), parent.name.asChars(), parentLen) != 0) return false;
      // Exact match or followed by a dot separator.
      return name.size() == parentLen || name.asChars()[parentLen] == '.';
    }

    bool empty() const noexcept { return name.size() == 0; }

    const char *c_str() const noexcept { return name.asChars(); }
  };

  /// A container for gameplay tags with efficient add, remove, has, and
  /// hierarchical query operations.
  ///
  /// Tags are stored in sorted order to enable fast lookup and range queries.
  /// The container is designed for moderate tag counts per entity (typically
  /// under 64 tags).
  class GameplayTagContainer {
  public:
    GameplayTagContainer() = default;

    /// Add a tag. Returns true if the tag was newly added, false if already present.
    bool add(const GameplayTag &tag) {
      auto it = std::lower_bound(_tags.begin(), _tags.end(), tag);
      if (it != _tags.end() && *it == tag) return false;
      _tags.insert(it, tag);
      return true;
    }

    /// Remove a tag. Returns true if the tag was present and removed.
    bool remove(const GameplayTag &tag) {
      auto it = std::lower_bound(_tags.begin(), _tags.end(), tag);
      if (it == _tags.end() || !(*it == tag)) return false;
      _tags.erase(it);
      return true;
    }

    /// Check whether the container holds exactly this tag.
    bool has(const GameplayTag &tag) const noexcept {
      auto it = std::lower_bound(_tags.begin(), _tags.end(), tag);
      return it != _tags.end() && *it == tag;
    }

    /// Check whether the container holds any tag that is a child of (or equal
    /// to) the given parent tag.
    bool has_any_child_of(const GameplayTag &parent) const noexcept {
      for (const auto &tag : _tags) {
        if (tag.is_child_of(parent)) return true;
      }
      return false;
    }

    /// Collect all tags that are children of the given parent.
    std::vector<GameplayTag> children_of(const GameplayTag &parent) const {
      std::vector<GameplayTag> result{};
      for (const auto &tag : _tags) {
        if (tag.is_child_of(parent)) result.push_back(tag);
      }
      return result;
    }

    /// Remove all tags that are children of (or equal to) the given parent.
    /// Returns the number of tags removed.
    size_t remove_children_of(const GameplayTag &parent) {
      size_t removed = 0;
      auto it = _tags.begin();
      while (it != _tags.end()) {
        if (it->is_child_of(parent)) {
          it = _tags.erase(it);
          ++removed;
        } else {
          ++it;
        }
      }
      return removed;
    }

    /// Check whether this container has all tags that the other container has.
    bool has_all(const GameplayTagContainer &required) const noexcept {
      for (const auto &tag : required._tags) {
        if (!has(tag)) return false;
      }
      return true;
    }

    /// Check whether this container shares any tag with the other container.
    bool has_any(const GameplayTagContainer &other) const noexcept {
      for (const auto &tag : other._tags) {
        if (has(tag)) return true;
      }
      return false;
    }

    size_t size() const noexcept { return _tags.size(); }
    bool empty() const noexcept { return _tags.empty(); }
    void clear() { _tags.clear(); }

    const std::vector<GameplayTag> &tags() const noexcept { return _tags; }

  private:
    std::vector<GameplayTag> _tags{};
  };

}  // namespace zs
