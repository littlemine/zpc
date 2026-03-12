// Copyright (c) zpc contributors. Licensed under the MIT License.
// ZpcReflectBlueprint.hpp — Blueprint metadata extensions for reflected types.
//
// Extends the core reflection system with metadata required for visual
// node-graph (blueprint) programming:
//   - Pin descriptors (inputs / outputs with typed connections)
//   - Node descriptors (category, color, display name)
//   - Port type classification for connection validation
//
// Usage:
//   1. Define a BlueprintNodeDescriptor with pins for your type.
//   2. Register it with BlueprintRegistry::registerNode().
//   3. The editor's GraphPanel queries the registry to build visual nodes.
//
// This header is std-free (uses zpc intrusive list + SmallString).

#pragma once

#include "ZpcReflectRuntime.hpp"
#include "ZpcReflectRegistry.hpp"
#include "ZpcReflectObject.hpp"

namespace zs {
namespace reflect {
namespace blueprint {

  // -----------------------------------------------------------------------
  // Pin direction
  // -----------------------------------------------------------------------

  enum class PinDirection : u8 {
    Input = 0,
    Output = 1,
  };

  // -----------------------------------------------------------------------
  // Pin data kind — classifies what flows through a connection
  // -----------------------------------------------------------------------

  enum class PinKind : u8 {
    Flow = 0,       ///< Execution flow (white arrow — no data, just ordering)
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Mat4,
    Entity,         ///< ECS entity handle
    Resource,       ///< ExecutionGraph ResourceHandle
    Object,         ///< Any reflect::Object (polymorphic)
    Delegate,       ///< Callable / function reference
    Array,          ///< Array / vector of sub-type
    Custom,         ///< User-defined (typeHash identifies the concrete type)
  };

  // -----------------------------------------------------------------------
  // Pin descriptor
  // -----------------------------------------------------------------------

  /// Describes one input or output pin on a blueprint node.
  struct PinDescriptor {
    const char*   name;          ///< Display name (e.g. "Delta Time")
    const char*   tooltip;       ///< Optional tooltip (nullptr = none)
    PinDirection  direction;
    PinKind       kind;
    u64           typeHash;      ///< For Custom kind: FNV-1a hash of the concrete type
    const char*   defaultValue;  ///< Stringified default (nullptr = none)
    bool          required;      ///< If true, node cannot execute without this connection
  };

  // -----------------------------------------------------------------------
  // Node category (for palette grouping)
  // -----------------------------------------------------------------------

  enum class NodeCategory : u8 {
    Uncategorized = 0,
    Flow,           ///< Control flow (branch, loop, sequence)
    Math,           ///< Arithmetic, trigonometry, linear algebra
    Logic,          ///< Boolean logic, comparison
    Entity,         ///< ECS entity operations
    Component,      ///< Component get/set/has
    System,         ///< System registration/query
    Resource,       ///< Resource handle operations
    Graph,          ///< ExecutionGraph / TaskGraph operations
    Render,         ///< Render pipeline operations
    IO,             ///< File / network / console
    Event,          ///< Event dispatch / listen
    Custom,         ///< User-defined category
  };

  // -----------------------------------------------------------------------
  // Node descriptor — complete blueprint metadata for one reflected type
  // -----------------------------------------------------------------------

  struct NodeDescriptor {
    const char*    typeName;       ///< Qualified C++ type name (matches TypeInfo::name)
    const char*    displayName;    ///< Short editor-friendly name
    const char*    category;       ///< Palette category string (e.g. "Graph/Pass")
    NodeCategory   categoryEnum;   ///< Enum for fast filtering
    u32            color;          ///< RGBA packed color for the node header
    const char*    tooltip;        ///< Optional node tooltip

    const PinDescriptor* pins;     ///< Array of pin descriptors
    int                  pinCount;

    u64            typeHash;       ///< FNV-1a hash of typeName (for fast lookup)
    bool           isPure;         ///< Pure node (no side effects, no flow pins)
    bool           isEvent;        ///< Event node (has implicit flow-out on trigger)
    bool           isLatent;       ///< Latent node (async, resumes on completion)

    /// Find a pin by name.
    const PinDescriptor* findPin(const char* n) const {
      if (!n || !pins) return nullptr;
      for (int i = 0; i < pinCount; ++i) {
        const char* a = pins[i].name;
        const char* b = n;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == *b) return &pins[i];
      }
      return nullptr;
    }

    /// Count input pins.
    int inputCount() const {
      int c = 0;
      for (int i = 0; i < pinCount; ++i)
        if (pins[i].direction == PinDirection::Input) ++c;
      return c;
    }

    /// Count output pins.
    int outputCount() const {
      int c = 0;
      for (int i = 0; i < pinCount; ++i)
        if (pins[i].direction == PinDirection::Output) ++c;
      return c;
    }
  };

  // -----------------------------------------------------------------------
  // Blueprint registry — global registry of blueprint-capable types
  // -----------------------------------------------------------------------

  struct NodeRegistryEntry {
    const NodeDescriptor* descriptor;
    NodeRegistryEntry*    next;
  };

  class BlueprintRegistry {
  public:
    static void registerNode(const NodeDescriptor* desc) {
      if (!desc) return;
      if (findByHash(desc->typeHash)) return;
      auto* entry = ::new NodeRegistryEntry{desc, head_};
      head_ = entry;
    }

    static void unregisterNode(u64 hash) {
      NodeRegistryEntry** pp = &head_;
      while (*pp) {
        if ((*pp)->descriptor && (*pp)->descriptor->typeHash == hash) {
          *pp = (*pp)->next;
          return;
        }
        pp = &(*pp)->next;
      }
    }

    static const NodeDescriptor* findByName(const char* name) {
      if (!name) return nullptr;
      for (auto* e = head_; e; e = e->next) {
        if (e->descriptor && detail::str_eq(e->descriptor->typeName, name))
          return e->descriptor;
      }
      return nullptr;
    }

    static const NodeDescriptor* findByHash(u64 hash) {
      for (auto* e = head_; e; e = e->next)
        if (e->descriptor && e->descriptor->typeHash == hash) return e->descriptor;
      return nullptr;
    }

    static const NodeDescriptor* findByDisplayName(const char* displayName) {
      if (!displayName) return nullptr;
      for (auto* e = head_; e; e = e->next) {
        if (e->descriptor && detail::str_eq(e->descriptor->displayName, displayName))
          return e->descriptor;
      }
      return nullptr;
    }

    template <typename Fn>
    static void forEach(Fn&& fn) {
      for (auto* e = head_; e; e = e->next)
        if (e->descriptor) fn(e->descriptor);
    }

    static int count() {
      int n = 0;
      for (auto* e = head_; e; e = e->next) ++n;
      return n;
    }

    /// Filter by category enum.
    template <typename Fn>
    static void forEachInCategory(NodeCategory cat, Fn&& fn) {
      for (auto* e = head_; e; e = e->next)
        if (e->descriptor && e->descriptor->categoryEnum == cat)
          fn(e->descriptor);
    }

  private:
    static inline NodeRegistryEntry* head_ = nullptr;
  };

  // -----------------------------------------------------------------------
  // Auto-registration helper
  // -----------------------------------------------------------------------

  struct AutoRegisterNode {
    explicit AutoRegisterNode(const NodeDescriptor* desc) {
      BlueprintRegistry::registerNode(desc);
    }
  };

  // -----------------------------------------------------------------------
  // Pin color helpers (for editor rendering)
  // -----------------------------------------------------------------------

  /// Default pin colors by kind (RGBA packed, 0xRRGGBBAA).
  inline constexpr u32 pinColor(PinKind kind) {
    switch (kind) {
      case PinKind::Flow:     return 0xFFFFFFFF;  // white
      case PinKind::Bool:     return 0xCC0000FF;  // red
      case PinKind::Int:      return 0x00CC99FF;  // teal
      case PinKind::Float:    return 0x66FF66FF;  // green
      case PinKind::String:   return 0xFF66CCFF;  // pink
      case PinKind::Vec2:     return 0xFFCC00FF;  // gold
      case PinKind::Vec3:     return 0xFFCC00FF;  // gold
      case PinKind::Vec4:     return 0xFFCC00FF;  // gold
      case PinKind::Mat4:     return 0xCC99FFFF;  // purple
      case PinKind::Entity:   return 0x3399FFFF;  // blue
      case PinKind::Resource: return 0xFF9933FF;  // orange
      case PinKind::Object:   return 0x9999CCFF;  // slate
      case PinKind::Delegate: return 0xFF3333FF;  // bright red
      case PinKind::Array:    return 0x66CCCCFF;  // cyan
      case PinKind::Custom:   return 0xAAAAAAFF;  // gray
    }
    return 0xAAAAAAFF;
  }

  /// Check if two pins can connect (basic type compatibility).
  inline bool canConnect(const PinDescriptor& output, const PinDescriptor& input) {
    if (output.direction != PinDirection::Output) return false;
    if (input.direction != PinDirection::Input) return false;
    // Flow connects to flow only.
    if (output.kind == PinKind::Flow) return input.kind == PinKind::Flow;
    if (input.kind == PinKind::Flow) return false;
    // Object accepts any non-flow type.
    if (input.kind == PinKind::Object) return true;
    // Same kind connects.
    if (output.kind == input.kind) {
      // For Custom, also check typeHash.
      if (output.kind == PinKind::Custom)
        return output.typeHash == input.typeHash;
      return true;
    }
    // Numeric implicit conversions.
    auto isNumeric = [](PinKind k) {
      return k == PinKind::Bool || k == PinKind::Int || k == PinKind::Float;
    };
    if (isNumeric(output.kind) && isNumeric(input.kind)) return true;
    // Vec2/3/4 are compatible with each other (truncation/extension).
    auto isVec = [](PinKind k) {
      return k == PinKind::Vec2 || k == PinKind::Vec3 || k == PinKind::Vec4;
    };
    if (isVec(output.kind) && isVec(input.kind)) return true;
    return false;
  }

}  // namespace blueprint
}  // namespace reflect
}  // namespace zs
