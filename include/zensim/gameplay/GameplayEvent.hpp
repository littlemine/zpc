#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include "zensim/TypeAlias.hpp"
#include "zensim/gameplay/GameplayEntity.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

  /// Unique identifier for event types. Each distinct event kind gets a unique id.
  struct GameplayEventTypeId {
    u32 value{0};

    constexpr GameplayEventTypeId() noexcept = default;
    constexpr explicit GameplayEventTypeId(u32 v) noexcept : value{v} {}

    bool operator==(const GameplayEventTypeId &other) const noexcept { return value == other.value; }
    bool operator!=(const GameplayEventTypeId &other) const noexcept { return value != other.value; }
  };

  struct GameplayEventTypeIdHash {
    size_t operator()(const GameplayEventTypeId &id) const noexcept {
      return static_cast<size_t>(id.value);
    }
  };

  /// A gameplay event carries a type, source, target, and a small payload.
  ///
  /// Events are value types designed to be dispatched, queued, and recorded.
  /// The payload is intentionally kept small and simple for the foundation
  /// stage. Later stages may introduce typed payloads via component-like
  /// extension.
  struct GameplayEvent {
    GameplayEventTypeId typeId{};
    SmallString typeName{};
    GameplayEntityId source{};
    GameplayEntityId target{};
    u64 timestamp{0};

    /// Small numeric payload for simple events. Higher-level events can
    /// extend this with richer typed data.
    double numericValue{0.0};
    SmallString stringValue{};
  };

  /// Subscription handle for event listeners.
  struct GameplayEventSubscription {
    u64 id{0};
    bool valid() const noexcept { return id != 0; }
  };

  /// Priority for event subscriber ordering. Lower values run first.
  enum class GameplayEventPriority : u8 { highest = 0, high = 64, normal = 128, low = 192, lowest = 255 };

  /// The gameplay event dispatcher manages subscriptions and event delivery.
  ///
  /// Subscribers register with a type filter (or all types) and a priority.
  /// Events are dispatched synchronously in priority order. An event history
  /// buffer records recent events for debugging and replay.
  class GameplayEventDispatcher {
  public:
    using EventHandler = std::function<void(const GameplayEvent &)>;

    GameplayEventDispatcher() = default;

    /// Subscribe to events of a specific type. Returns a handle for unsubscription.
    GameplayEventSubscription subscribe(GameplayEventTypeId typeId, EventHandler handler,
                                         GameplayEventPriority priority
                                         = GameplayEventPriority::normal) {
      GameplayEventSubscription sub{++_nextSubId};
      _subscribers.push_back(
          Subscriber{sub, typeId, static_cast<EventHandler &&>(handler), priority, false});
      _sorted = false;
      return sub;
    }

    /// Subscribe to all event types.
    GameplayEventSubscription subscribe_all(EventHandler handler,
                                             GameplayEventPriority priority
                                             = GameplayEventPriority::normal) {
      GameplayEventSubscription sub{++_nextSubId};
      _subscribers.push_back(Subscriber{
          sub, GameplayEventTypeId{}, static_cast<EventHandler &&>(handler), priority, true});
      _sorted = false;
      return sub;
    }

    /// Unsubscribe by handle. Returns true if found and removed.
    bool unsubscribe(GameplayEventSubscription sub) {
      for (auto it = _subscribers.begin(); it != _subscribers.end(); ++it) {
        if (it->subscription.id == sub.id) {
          _subscribers.erase(it);
          return true;
        }
      }
      return false;
    }

    /// Dispatch an event to all matching subscribers in priority order.
    void dispatch(const GameplayEvent &event) {
      ensure_sorted();
      for (const auto &subscriber : _subscribers) {
        if (subscriber.allTypes || subscriber.typeFilter == event.typeId) {
          subscriber.handler(event);
        }
      }
      record_to_history(event);
    }

    /// Emit a simple event with minimal setup.
    void emit(GameplayEventTypeId typeId, const SmallString &typeName, GameplayEntityId source,
              GameplayEntityId target = {}, double numericValue = 0.0) {
      GameplayEvent event{};
      event.typeId = typeId;
      event.typeName = typeName;
      event.source = source;
      event.target = target;
      event.timestamp = _eventCounter++;
      event.numericValue = numericValue;
      dispatch(event);
    }

    // ---- History ----

    /// Set the maximum number of events to retain in history.
    void set_history_capacity(size_t capacity) {
      _historyCapacity = capacity;
      while (_history.size() > _historyCapacity) {
        _history.erase(_history.begin());
      }
    }

    size_t history_capacity() const noexcept { return _historyCapacity; }

    const std::vector<GameplayEvent> &history() const noexcept { return _history; }

    void clear_history() { _history.clear(); }

    size_t subscriber_count() const noexcept { return _subscribers.size(); }

  private:
    struct Subscriber {
      GameplayEventSubscription subscription{};
      GameplayEventTypeId typeFilter{};
      EventHandler handler{};
      GameplayEventPriority priority{GameplayEventPriority::normal};
      bool allTypes{false};
    };

    void ensure_sorted() {
      if (_sorted) return;
      std::stable_sort(_subscribers.begin(), _subscribers.end(),
                       [](const Subscriber &a, const Subscriber &b) {
                         return static_cast<u8>(a.priority) < static_cast<u8>(b.priority);
                       });
      _sorted = true;
    }

    void record_to_history(const GameplayEvent &event) {
      if (_historyCapacity == 0) return;
      if (_history.size() >= _historyCapacity) {
        _history.erase(_history.begin());
      }
      _history.push_back(event);
    }

    std::vector<Subscriber> _subscribers{};
    std::vector<GameplayEvent> _history{};
    size_t _historyCapacity{256};
    u64 _nextSubId{0};
    u64 _eventCounter{0};
    bool _sorted{true};
  };

}  // namespace zs
