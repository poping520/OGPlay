#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {

// Session-owned side state for the bounded java.util collection facade.
// Guest-visible identities remain ordinary VmObjectRef values; no host
// container address is exposed to Java code.
class CollectionRuntime final {
public:
  struct SequenceState final {
    std::vector<VmObjectRef> elements;
    std::uint64_t mod_count{};
  };

  struct MapNode final {
    std::uint64_t id{};
    VmObjectRef key;
    VmObjectRef value;
    std::int32_t hash{};
  };

  struct MapState final {
    std::vector<MapNode> entries;
    std::uint64_t mod_count{};
    std::uint64_t next_id{1};
    bool access_order{};
  };

  enum class ViewKind : std::uint8_t { keys, values, entries };
  struct MapViewState final {
    VmObjectRef owner;
    ViewKind kind{ViewKind::keys};
  };

  struct EntryState final {
    VmObjectRef owner;
    std::uint64_t entry_id{};
  };

  enum class IteratorKind : std::uint8_t {
    sequence,
    sequence_reverse,
    map_keys,
    map_values,
    map_entries,
  };
  struct IteratorState final {
    VmObjectRef owner;
    IteratorKind kind{IteratorKind::sequence};
    std::size_t cursor{};
    std::optional<std::size_t> last_index;
    std::optional<std::uint64_t> last_entry_id;
    std::uint64_t expected_mod_count{};
  };

  struct SubListState final {
    VmObjectRef owner;
    std::size_t offset{};
    std::size_t size{};
    std::uint64_t expected_mod_count{};
  };

  [[nodiscard]] SequenceState &EnsureSequence(VmObjectRef owner);
  [[nodiscard]] SequenceState *FindSequence(VmObjectRef owner) noexcept;
  [[nodiscard]] const SequenceState *
  FindSequence(VmObjectRef owner) const noexcept;

  [[nodiscard]] MapState &EnsureMap(VmObjectRef owner);
  [[nodiscard]] MapState *FindMap(VmObjectRef owner) noexcept;
  [[nodiscard]] const MapState *FindMap(VmObjectRef owner) const noexcept;

  void SetMapView(VmObjectRef view, MapViewState state);
  [[nodiscard]] MapViewState *FindMapView(VmObjectRef view) noexcept;
  [[nodiscard]] const MapViewState *
  FindMapView(VmObjectRef view) const noexcept;

  void SetEntry(VmObjectRef entry, EntryState state);
  [[nodiscard]] EntryState *FindEntry(VmObjectRef entry) noexcept;
  [[nodiscard]] const EntryState *FindEntry(VmObjectRef entry) const noexcept;

  void SetIterator(VmObjectRef iterator, IteratorState state);
  [[nodiscard]] IteratorState *FindIterator(VmObjectRef iterator) noexcept;
  [[nodiscard]] const IteratorState *
  FindIterator(VmObjectRef iterator) const noexcept;

  void SetSubList(VmObjectRef view, SubListState state);
  [[nodiscard]] SubListState *FindSubList(VmObjectRef view) noexcept;
  [[nodiscard]] const SubListState *
  FindSubList(VmObjectRef view) const noexcept;

  void Trace(VmObjectRef owner,
             const std::function<void(VmObjectRef)> &visit) const;
  void Sweep(VmObjectRef owner);
  void Clone(VmObjectRef source, VmObjectRef clone);

private:
  std::unordered_map<std::uint32_t, SequenceState> sequences_;
  std::unordered_map<std::uint32_t, MapState> maps_;
  std::unordered_map<std::uint32_t, MapViewState> map_views_;
  std::unordered_map<std::uint32_t, EntryState> entries_;
  std::unordered_map<std::uint32_t, IteratorState> iterators_;
  std::unordered_map<std::uint32_t, SubListState> sub_lists_;
};

} // namespace ogplay::runtime::dexvm
