#include "ogplay/runtime/dexvm/collection_runtime.h"

namespace ogplay::runtime::dexvm {
namespace {

template <typename Table>
[[nodiscard]] auto *Find(Table &table, const VmObjectRef owner) noexcept {
  const auto found = table.find(owner.Value());
  return found == table.end() ? nullptr : &found->second;
}

template <typename Table>
[[nodiscard]] const auto *Find(const Table &table,
                               const VmObjectRef owner) noexcept {
  const auto found = table.find(owner.Value());
  return found == table.end() ? nullptr : &found->second;
}

} // namespace

CollectionRuntime::SequenceState &
CollectionRuntime::EnsureSequence(const VmObjectRef owner) {
  return sequences_[owner.Value()];
}

CollectionRuntime::SequenceState *
CollectionRuntime::FindSequence(const VmObjectRef owner) noexcept {
  return Find(sequences_, owner);
}

const CollectionRuntime::SequenceState *
CollectionRuntime::FindSequence(const VmObjectRef owner) const noexcept {
  return Find(sequences_, owner);
}

CollectionRuntime::MapState &
CollectionRuntime::EnsureMap(const VmObjectRef owner) {
  return maps_[owner.Value()];
}

CollectionRuntime::MapState *
CollectionRuntime::FindMap(const VmObjectRef owner) noexcept {
  return Find(maps_, owner);
}

const CollectionRuntime::MapState *
CollectionRuntime::FindMap(const VmObjectRef owner) const noexcept {
  return Find(maps_, owner);
}

void CollectionRuntime::SetMapView(const VmObjectRef view, MapViewState state) {
  map_views_[view.Value()] = state;
}

CollectionRuntime::MapViewState *
CollectionRuntime::FindMapView(const VmObjectRef view) noexcept {
  return Find(map_views_, view);
}

const CollectionRuntime::MapViewState *
CollectionRuntime::FindMapView(const VmObjectRef view) const noexcept {
  return Find(map_views_, view);
}

void CollectionRuntime::SetEntry(const VmObjectRef entry, EntryState state) {
  entries_[entry.Value()] = state;
}

CollectionRuntime::EntryState *
CollectionRuntime::FindEntry(const VmObjectRef entry) noexcept {
  return Find(entries_, entry);
}

const CollectionRuntime::EntryState *
CollectionRuntime::FindEntry(const VmObjectRef entry) const noexcept {
  return Find(entries_, entry);
}

void CollectionRuntime::SetIterator(const VmObjectRef iterator,
                                    IteratorState state) {
  iterators_[iterator.Value()] = state;
}

CollectionRuntime::IteratorState *
CollectionRuntime::FindIterator(const VmObjectRef iterator) noexcept {
  return Find(iterators_, iterator);
}

const CollectionRuntime::IteratorState *
CollectionRuntime::FindIterator(const VmObjectRef iterator) const noexcept {
  return Find(iterators_, iterator);
}

void CollectionRuntime::SetSubList(const VmObjectRef view, SubListState state) {
  sub_lists_[view.Value()] = state;
}

CollectionRuntime::SubListState *
CollectionRuntime::FindSubList(const VmObjectRef view) noexcept {
  return Find(sub_lists_, view);
}

const CollectionRuntime::SubListState *
CollectionRuntime::FindSubList(const VmObjectRef view) const noexcept {
  return Find(sub_lists_, view);
}

void CollectionRuntime::Trace(
    const VmObjectRef owner,
    const std::function<void(VmObjectRef)> &visit) const {
  if (const auto *sequence = FindSequence(owner); sequence != nullptr) {
    for (const auto element : sequence->elements)
      visit(element);
  }
  if (const auto *map = FindMap(owner); map != nullptr) {
    for (const auto &entry : map->entries) {
      visit(entry.key);
      visit(entry.value);
    }
  }
  if (const auto *view = FindMapView(owner); view != nullptr) {
    visit(view->owner);
  }
  if (const auto *entry = FindEntry(owner); entry != nullptr) {
    visit(entry->owner);
  }
  if (const auto *iterator = FindIterator(owner); iterator != nullptr) {
    visit(iterator->owner);
  }
  if (const auto *sub_list = FindSubList(owner); sub_list != nullptr) {
    visit(sub_list->owner);
  }
}

void CollectionRuntime::Sweep(const VmObjectRef owner) {
  const auto key = owner.Value();
  sequences_.erase(key);
  maps_.erase(key);
  map_views_.erase(key);
  entries_.erase(key);
  iterators_.erase(key);
  sub_lists_.erase(key);
}

void CollectionRuntime::Clone(const VmObjectRef source,
                              const VmObjectRef clone) {
  if (const auto *sequence = FindSequence(source); sequence != nullptr) {
    sequences_[clone.Value()] = *sequence;
  }
  if (const auto *map = FindMap(source); map != nullptr) {
    maps_[clone.Value()] = *map;
  }
}

} // namespace ogplay::runtime::dexvm
